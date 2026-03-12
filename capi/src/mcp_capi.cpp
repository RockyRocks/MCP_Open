#include "mcp_capi.h"

#include "core/Config.h"
#include "core/ProtocolHandler.h"
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

#include <memory>
#include <string>
#include <cstring>

static thread_local std::string g_lastError;

static void setError(const std::string& msg) {
    g_lastError = msg;
}

static int copyToBuffer(const std::string& src, char* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) {
        setError("Invalid buffer");
        return -1;
    }
    int needed = static_cast<int>(src.size());
    if (needed >= bufferSize) {
        setError("Buffer too small (need " + std::to_string(needed + 1) + " bytes)");
        std::memcpy(buffer, src.c_str(), static_cast<size_t>(bufferSize - 1));
        buffer[bufferSize - 1] = '\0';
        return -1;
    }
    std::memcpy(buffer, src.c_str(), static_cast<size_t>(needed + 1));
    return needed;
}

struct McpServerContext {
    Config config;
    std::shared_ptr<HttplibClient> httpClient;
    std::shared_ptr<LiteLLMProvider> llmProvider;
    std::shared_ptr<SkillEngine> skillEngine;
    std::shared_ptr<McpServerRegistry> mcpRegistry;
    std::shared_ptr<CommandRegistry> commandRegistry;
    std::shared_ptr<RateLimiter> rateLimiter;
    std::shared_ptr<ApiKeyValidator> apiKeyValidator;
    std::shared_ptr<ProtocolHandler> protocolHandler;
};

extern "C" {

McpServerHandle mcp_server_create(const char* configPath) {
    try {
        auto ctx = new McpServerContext();

        if (configPath && std::strlen(configPath) > 0) {
            try {
                ctx->config = Config::loadFromFile(configPath);
            } catch (...) {
                ctx->config = Config::loadFromEnv();
            }
        } else {
            ctx->config = Config::loadFromEnv();
        }

        ctx->httpClient = std::make_shared<HttplibClient>();
        ctx->llmProvider = std::make_shared<LiteLLMProvider>(ctx->httpClient, ctx->config);

        ctx->skillEngine = std::make_shared<SkillEngine>();
        ctx->skillEngine->loadFromDirectory(ctx->config.skillsDirectory());

        ctx->mcpRegistry = std::make_shared<McpServerRegistry>();
        try {
            *ctx->mcpRegistry = McpServerRegistry::loadFromFile(ctx->config.mcpServersConfigPath());
        } catch (...) {}

        ctx->commandRegistry = std::make_shared<CommandRegistry>();
        ctx->commandRegistry->registerCommand("echo", createEchoCommand());
        ctx->commandRegistry->registerCommand("llm", std::make_shared<LLMCommand>(ctx->llmProvider));
        ctx->commandRegistry->registerCommand("skill",
            std::make_shared<SkillCommand>(ctx->skillEngine, ctx->llmProvider));
        ctx->commandRegistry->registerCommand("remote",
            std::make_shared<CompositeCommand>(ctx->mcpRegistry, ctx->httpClient));

        ctx->rateLimiter = std::make_shared<RateLimiter>(ctx->config.rateLimitRequestsPerMinute());
        ctx->apiKeyValidator = std::make_shared<ApiKeyValidator>(
            ctx->config.authApiKey(), ctx->config.authEnabled());
        ctx->protocolHandler = std::make_shared<ProtocolHandler>(
            ctx->commandRegistry, ctx->rateLimiter, ctx->apiKeyValidator,
            ctx->config.maxRequestBodySize());

        return static_cast<McpServerHandle>(ctx);
    } catch (const std::exception& e) {
        setError(e.what());
        return nullptr;
    }
}

void mcp_server_destroy(McpServerHandle handle) {
    delete static_cast<McpServerContext*>(handle);
}

int mcp_server_start(McpServerHandle /*handle*/, int /*port*/) {
    setError("Use mcp_handle_request for embedded mode; standalone server via mcp_server executable");
    return -1;
}

void mcp_server_stop(McpServerHandle /*handle*/) {
    // No-op for embedded mode
}

int mcp_command_list(McpServerHandle handle, char* buffer, int bufferSize) {
    if (!handle) { setError("Null handle"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    nlohmann::json cmds = ctx->commandRegistry->listCommands();
    return copyToBuffer(cmds.dump(), buffer, bufferSize);
}

int mcp_command_has(McpServerHandle handle, const char* name) {
    if (!handle || !name) { setError("Null argument"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    return ctx->commandRegistry->hasCommand(name) ? 1 : 0;
}

int mcp_handle_request(McpServerHandle handle, const char* jsonBody,
                        const char* clientIp, char* responseBuffer, int bufferSize) {
    if (!handle || !jsonBody) { setError("Null argument"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    std::string ip = clientIp ? clientIp : "0.0.0.0";
    std::string result = ctx->protocolHandler->handleRequest(jsonBody, ip);
    return copyToBuffer(result, responseBuffer, bufferSize);
}

int mcp_llm_complete(McpServerHandle handle, const char* requestJson,
                      char* responseBuffer, int bufferSize) {
    if (!handle || !requestJson) { setError("Null argument"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    try {
        auto req = nlohmann::json::parse(requestJson);
        nlohmann::json fullReq = {{"command", "llm"}, {"payload", req}};
        auto cmd = ctx->commandRegistry->resolve("llm");
        if (!cmd) { setError("LLM command not registered"); return -1; }
        auto future = cmd->executeAsync(fullReq);
        auto result = future.get();
        return copyToBuffer(result.dump(), responseBuffer, bufferSize);
    } catch (const std::exception& e) {
        setError(e.what());
        return -1;
    }
}

int mcp_skill_list(McpServerHandle handle, char* buffer, int bufferSize) {
    if (!handle) { setError("Null handle"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    return copyToBuffer(ctx->skillEngine->listSkillsJson().dump(), buffer, bufferSize);
}

int mcp_skill_execute(McpServerHandle handle, const char* requestJson,
                       char* responseBuffer, int bufferSize) {
    if (!handle || !requestJson) { setError("Null argument"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    try {
        auto req = nlohmann::json::parse(requestJson);
        nlohmann::json fullReq = {{"command", "skill"}, {"payload", req}};
        auto cmd = ctx->commandRegistry->resolve("skill");
        if (!cmd) { setError("Skill command not registered"); return -1; }
        auto future = cmd->executeAsync(fullReq);
        auto result = future.get();
        return copyToBuffer(result.dump(), responseBuffer, bufferSize);
    } catch (const std::exception& e) {
        setError(e.what());
        return -1;
    }
}

int mcp_server_list_remote(McpServerHandle handle, char* buffer, int bufferSize) {
    if (!handle) { setError("Null handle"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    return copyToBuffer(ctx->mcpRegistry->toJson().dump(), buffer, bufferSize);
}

const char* mcp_last_error(void) {
    return g_lastError.c_str();
}

} // extern "C"
