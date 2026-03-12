#include "core/Config.h"
#include "core/Logger.h"
#include "core/ProtocolHandler.h"
#include "core/ThreadPool.h"
#include "commands/CommandRegistry.h"
#include "commands/EchoCommand.h"
#include "llm/LiteLLMProvider.h"
#include "llm/LLMCommand.h"
#include "discovery/McpServerRegistry.h"
#include "discovery/CompositeCommand.h"
#include "skills/SkillEngine.h"
#include "skills/SkillCommand.h"
#include "http/HttplibClient.h"
#include "security/RateLimiter.h"
#include "security/ApiKeyValidator.h"
#include "server/IServer.h"
#include "server/HttplibServer.h"

#ifdef USE_UWS
#include "server/UwsServer.h"
#endif

#include <iostream>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    // Determine config path from CLI args or default
    std::string configPath = "config/mcp_config.json";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        }
    }

    // Load config (file if exists, otherwise env)
    Config config;
    try {
        config = Config::loadFromFile(configPath);
        Logger::getInstance().log("Config loaded from " + configPath);
    } catch (...) {
        Logger::getInstance().log("Config file not found, using environment variables");
        config = Config::loadFromEnv();
    }

    // HTTP client for outbound calls
    auto httpClient = std::make_shared<HttplibClient>();

    // LLM provider (LiteLLM proxy)
    auto llmProvider = std::make_shared<LiteLLMProvider>(httpClient, config);

    // Skills engine
    auto skillEngine = std::make_shared<SkillEngine>();
    skillEngine->loadFromDirectory(config.skillsDirectory());

    // MCP server discovery
    auto mcpRegistry = std::make_shared<McpServerRegistry>();
    try {
        *mcpRegistry = McpServerRegistry::loadFromFile(config.mcpServersConfigPath());
    } catch (const std::exception& e) {
        Logger::getInstance().log(std::string("MCP servers config: ") + e.what());
    }

    // Command registry
    auto commandRegistry = std::make_shared<CommandRegistry>();
    commandRegistry->registerCommand("echo", createEchoCommand());
    commandRegistry->registerCommand("llm", std::make_shared<LLMCommand>(llmProvider));
    commandRegistry->registerCommand("skill", std::make_shared<SkillCommand>(skillEngine, llmProvider));
    commandRegistry->registerCommand("remote", std::make_shared<CompositeCommand>(mcpRegistry, httpClient));

    Logger::getInstance().log("Registered commands: echo, llm, skill, remote");

    // Security
    auto rateLimiter = std::make_shared<RateLimiter>(config.rateLimitRequestsPerMinute());
    auto apiKeyValidator = std::make_shared<ApiKeyValidator>(
        config.authApiKey(), config.authEnabled());

    // Protocol handler
    auto protocolHandler = std::make_shared<ProtocolHandler>(
        commandRegistry, rateLimiter, apiKeyValidator, config.maxRequestBodySize());

    // Server
    std::unique_ptr<IServer> server;
#ifdef USE_UWS
    server = std::make_unique<UwsServer>();
    Logger::getInstance().log("Using uWebSockets server");
#else
    server = std::make_unique<HttplibServer>();
    Logger::getInstance().log("Using httplib server");
#endif

    // Routes
    server->addRoute("POST", "/mcp",
        [protocolHandler](const std::string& body, const std::string& clientIp,
                           std::function<void(int, const std::string&)> respond) {
            auto result = protocolHandler->handleRequest(body, clientIp);

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

    server->addRoute("GET", "/health",
        [](const std::string&, const std::string&,
           std::function<void(int, const std::string&)> respond) {
            respond(200, R"({"status":"ok"})");
        });

    server->addRoute("GET", "/skills",
        [skillEngine](const std::string&, const std::string&,
                       std::function<void(int, const std::string&)> respond) {
            respond(200, skillEngine->listSkillsJson().dump());
        });

    server->addRoute("GET", "/servers",
        [mcpRegistry](const std::string&, const std::string&,
                       std::function<void(int, const std::string&)> respond) {
            respond(200, mcpRegistry->toJson().dump());
        });

    server->addRoute("GET", "/commands",
        [commandRegistry](const std::string&, const std::string&,
                           std::function<void(int, const std::string&)> respond) {
            nlohmann::json cmds = commandRegistry->listCommands();
            respond(200, cmds.dump());
        });

    int port = config.serverPort();
    Logger::getInstance().log("Starting server on port " + std::to_string(port));
    server->listen("0.0.0.0", port);

    return 0;
}
