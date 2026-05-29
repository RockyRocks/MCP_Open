#include <core/Config.h>
#include <core/Logger.h>
#include <core/Version.h>
#include <core/ProtocolHandler.h>
#include <core/ThreadPool.h>
#include <commands/CommandRegistry.h>
#include <commands/EchoCommand.h>
#include <llm/LiteLLMProvider.h>
#include <llm/LLMCommand.h>
#include <discovery/McpServerRegistry.h>
#include <discovery/CompositeCommand.h>
#include <skills/SkillEngine.h>
#include <skills/SkillCommand.h>
#include <skills/SkillToolAdapter.h>
#include <skills/PluginLoader.h>
#include <plugins/NativePluginLoader.h>
#include <plugins/ScriptPluginLoader.h>
#include <http/HttplibClient.h>
#include <security/RateLimiter.h>
#include <security/ApiKeyValidator.h>
#include <server/IServer.h>
#include <server/HttplibServer.h>
#include <server/StdioTransport.h>

#ifdef USE_UWS
#include <server/UwsServer.h>
#endif

#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>

int main(int argc, char** argv) {
    // Determine config path and transport mode from CLI args
    std::string configPath = "config/mcp_config.json";
    bool stdioMode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--stdio") {
            stdioMode = true;
        }
    }

    // Load config (file if exists, otherwise env)
    Config config;
    try {
        config = Config::LoadFromFile(configPath);
        Logger::GetInstance().Log("Config loaded from " + configPath);
    } catch (...) {
        Logger::GetInstance().Log("Config file not found, using environment variables");
        config = Config::LoadFromEnv();
    }

    // HTTP client for outbound calls
    auto httpClient = std::make_shared<HttplibClient>();

    // LLM provider (LiteLLM proxy)
    auto llmProvider = std::make_shared<LiteLLMProvider>(httpClient, config);

    // Skills engine — load JSON skills then SKILL.md plugin skills
    auto skillEngine = std::make_shared<SkillEngine>();
    skillEngine->LoadFromDirectory(config.GetSkillsDirectory());
    auto pluginSkillNames = PluginLoader::LoadIntoEngine(config.GetPluginsDirectory(), *skillEngine);
    std::unordered_set<std::string> pluginSkillSet(pluginSkillNames.begin(),
                                                    pluginSkillNames.end());

    // MCP server discovery
    auto mcpRegistry = std::make_shared<McpServerRegistry>();
    try {
        *mcpRegistry = McpServerRegistry::LoadFromFile(config.GetMcpServersConfigPath());
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(std::string("MCP servers config: ") + e.what());
    }

    // Command registry — unified flat tool list
    auto commandRegistry = std::make_shared<CommandRegistry>();

    // Built-in defaults
    commandRegistry->RegisterCommand("echo", CreateEchoCommand());
    commandRegistry->RegisterCommand("llm", std::make_shared<LLMCommand>(llmProvider));
    commandRegistry->RegisterCommand("remote", std::make_shared<CompositeCommand>(mcpRegistry, httpClient));

    // "skill" meta-tool kept for backward compat but marked hidden
    commandRegistry->RegisterCommand("skill", std::make_shared<SkillCommand>(skillEngine, llmProvider));

    // Promote every skill (JSON + plugin) as its own first-class tool
    for (const auto& name : skillEngine->ListSkills()) {
        auto def = skillEngine->Resolve(name);
        if (!def.has_value()) continue;
        ToolSource source = pluginSkillSet.count(name) ? ToolSource::Plugin
                                                       : ToolSource::JsonSkill;
        commandRegistry->RegisterCommand(name,
            std::make_shared<SkillToolAdapter>(*def, llmProvider, source));
    }

    // Plugin loaders (instance-based for testability and multi-instance support)
    auto nativeLoader = std::make_shared<NativePluginLoader>();
    auto scriptLoader = std::make_shared<ScriptPluginLoader>();

    nativeLoader->LoadAll(config.GetPluginsDirectory(), *commandRegistry);
    scriptLoader->LoadAll(config.GetPluginsDirectory(), *commandRegistry);

    Logger::GetInstance().Log("Registered commands: echo, llm, remote, skill(hidden), +"
        + std::to_string(skillEngine->ListSkills().size()) + " skill tools");

    // stdio transport mode (MCP protocol over JSON-RPC 2.0)
    if (stdioMode) {
        // Use a shared_ptr so the watcher lambda can capture it safely
        auto transportPtr = std::make_shared<StdioTransport>(
            commandRegistry, skillEngine, mcpRegistry,
            std::cin, std::cout, "toolsmith", TOOLSMITH_VERSION_STRING);

        // When a new plugin is hot-loaded at runtime, push a standard MCP
        // notifications/tools/list_changed event so the LLM client refreshes
        // its tool list without needing to poll.
        nativeLoader->SetNotifyCallback(
            [transportPtr](const nlohmann::json& payload) {
                nlohmann::json notification = {
                    {"jsonrpc", "2.0"},
                    {"method",  "notifications/tools/list_changed"},
                    {"params",  payload}
                };
                transportPtr->PushNotification(notification);
            });

        scriptLoader->SetNotifyCallback(
            [transportPtr](const nlohmann::json& payload) {
                nlohmann::json notification = {
                    {"jsonrpc", "2.0"},
                    {"method",  "notifications/tools/list_changed"},
                    {"params",  payload}
                };
                transportPtr->PushNotification(notification);
            });

        nativeLoader->StartWatcher(config.GetPluginsDirectory(),
                                    commandRegistry);
        scriptLoader->StartWatcher(config.GetPluginsDirectory(),
                                    commandRegistry);

        transportPtr->Run();

        scriptLoader->StopWatcher();
        nativeLoader->StopWatcher();
        return 0;
    }

    // Security
    auto rateLimiter = std::make_shared<RateLimiter>(config.GetRateLimitRequestsPerMinute());
    auto apiKeyValidator = std::make_shared<ApiKeyValidator>(
        config.GetAuthApiKey(), config.IsAuthEnabled());

    // Protocol handler
    auto protocolHandler = std::make_shared<ProtocolHandler>(
        commandRegistry, rateLimiter, apiKeyValidator, config.GetMaxRequestBodySize());

    // Server
    std::unique_ptr<IServer> server;
#ifdef USE_UWS
    server = std::make_unique<UwsServer>();
    Logger::GetInstance().Log("Using uWebSockets server");
#else
    server = std::make_unique<HttplibServer>();
    Logger::GetInstance().Log("Using httplib server");
#endif

    // Routes
    server->AddRoute("POST", "/mcp",
        [protocolHandler](const std::string& body, const std::string& clientIp,
                           std::function<void(int, const std::string&)> respond) {
            auto result = protocolHandler->HandleRequest(body, clientIp);

            int status = 200;
            try {
                auto parsed = nlohmann::json::parse(result);
                if (parsed.value("error", "") == "Payload too large") status = 413;
                else if (parsed.value("error", "") == "Rate limit exceeded") status = 429;
                else if (parsed.value("error", "") == "Unauthorized") status = 401;
                else if (parsed.value("error", "") == "Invalid JSON") status = 400;
                else if (parsed.value("error", "") == "JSON nesting too deep") status = 400;
                else if (parsed.contains("error")) status = 400;
            } catch (...) {}

            respond(status, result);
        });

    server->AddRoute("GET", "/health",
        [](const std::string&, const std::string&,
           std::function<void(int, const std::string&)> respond) {
            respond(200, R"({"status":"ok"})");
        });

    server->AddRoute("GET", "/skills",
        [commandRegistry](const std::string&, const std::string&,
                           std::function<void(int, const std::string&)> respond) {
            // Return all skill tools (JsonSkill + Plugin) from the registry
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& meta : commandRegistry->ListToolMetadata()) {
                if (meta.m_Source == ToolSource::BuiltIn) continue;
                arr.push_back({
                    {"name", meta.m_Name},
                    {"description", meta.m_Description},
                    {"inputSchema", meta.m_InputSchema}
                });
            }
            respond(200, arr.dump());
        });

    server->AddRoute("GET", "/servers",
        [mcpRegistry](const std::string&, const std::string&,
                       std::function<void(int, const std::string&)> respond) {
            respond(200, mcpRegistry->ToJson().dump());
        });

    server->AddRoute("GET", "/commands",
        [commandRegistry](const std::string&, const std::string&,
                           std::function<void(int, const std::string&)> respond) {
            nlohmann::json cmds = commandRegistry->ListCommands();
            respond(200, cmds.dump());
        });

    nativeLoader->SetNotifyCallback([](const nlohmann::json& payload) {
        Logger::GetInstance().Log("[plugin_loaded] " + payload.dump());
    });
    scriptLoader->SetNotifyCallback([](const nlohmann::json& payload) {
        Logger::GetInstance().Log("[script_plugin_reloaded] " + payload.dump());
    });
    nativeLoader->StartWatcher(config.GetPluginsDirectory(), commandRegistry);
    scriptLoader->StartWatcher(config.GetPluginsDirectory(), commandRegistry);

    int port = config.GetServerPort();
    Logger::GetInstance().Log("Starting server on port " + std::to_string(port));
    server->Listen("0.0.0.0", port);

    scriptLoader->StopWatcher();
    nativeLoader->StopWatcher();
    return 0;
}
