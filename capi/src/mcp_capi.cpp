#include "mcp_capi.h"

#include <core/Config.h>
#include <core/ProtocolHandler.h>
#include <commands/CommandRegistry.h>
#include <commands/EchoCommand.h>
#include <llm/LiteLLMProvider.h>
#include <llm/LLMCommand.h>
#include <discovery/McpServerRegistry.h>
#include <discovery/CompositeCommand.h>
#include <skills/SkillEngine.h>
#include <skills/SkillCommand.h>
#include <http/HttplibClient.h>
#include <security/RateLimiter.h>
#include <security/ApiKeyValidator.h>

#include <memory>
#include <string>
#include <cstring>

static thread_local std::string g_LastError;

static void SetError(const std::string& msg) {
    g_LastError = msg;
}

static int CopyToBuffer(const std::string& src, char* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) {
        SetError("Invalid buffer");
        return -1;
    }
    int needed = static_cast<int>(src.size());
    if (needed >= bufferSize) {
        SetError("Buffer too small (need " + std::to_string(needed + 1) + " bytes)");
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
                ctx->config = Config::LoadFromFile(configPath);
            } catch (...) {
                ctx->config = Config::LoadFromEnv();
            }
        } else {
            ctx->config = Config::LoadFromEnv();
        }

        ctx->httpClient = std::make_shared<HttplibClient>();
        ctx->llmProvider = std::make_shared<LiteLLMProvider>(ctx->httpClient, ctx->config);

        ctx->skillEngine = std::make_shared<SkillEngine>();
        ctx->skillEngine->LoadFromDirectory(ctx->config.GetSkillsDirectory());

        ctx->mcpRegistry = std::make_shared<McpServerRegistry>();
        try {
            *ctx->mcpRegistry = McpServerRegistry::LoadFromFile(ctx->config.GetMcpServersConfigPath());
        } catch (...) {}

        ctx->commandRegistry = std::make_shared<CommandRegistry>();
        ctx->commandRegistry->RegisterCommand("echo", CreateEchoCommand());
        ctx->commandRegistry->RegisterCommand("llm", std::make_shared<LLMCommand>(ctx->llmProvider));
        ctx->commandRegistry->RegisterCommand("skill",
            std::make_shared<SkillCommand>(ctx->skillEngine, ctx->llmProvider));
        ctx->commandRegistry->RegisterCommand("remote",
            std::make_shared<CompositeCommand>(ctx->mcpRegistry, ctx->httpClient));

        ctx->rateLimiter = std::make_shared<RateLimiter>(ctx->config.GetRateLimitRequestsPerMinute());
        ctx->apiKeyValidator = std::make_shared<ApiKeyValidator>(
            ctx->config.GetAuthApiKey(), ctx->config.IsAuthEnabled());
        ctx->protocolHandler = std::make_shared<ProtocolHandler>(
            ctx->commandRegistry, ctx->rateLimiter, ctx->apiKeyValidator,
            ctx->config.GetMaxRequestBodySize());

        return static_cast<McpServerHandle>(ctx);
    } catch (const std::exception& e) {
        SetError(e.what());
        return nullptr;
    }
}

void mcp_server_destroy(McpServerHandle handle) {
    delete static_cast<McpServerContext*>(handle);
}

int mcp_server_start(McpServerHandle /*handle*/, int /*port*/) {
    SetError("Use mcp_handle_request for embedded mode; standalone server via mcp_server executable");
    return -1;
}

void mcp_server_stop(McpServerHandle /*handle*/) {
    // No-op for embedded mode
}

int mcp_command_list(McpServerHandle handle, char* buffer, int bufferSize) {
    if (!handle) { SetError("Null handle"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    nlohmann::json cmds = ctx->commandRegistry->ListCommands();
    return CopyToBuffer(cmds.dump(), buffer, bufferSize);
}

int mcp_command_has(McpServerHandle handle, const char* name) {
    if (!handle || !name) { SetError("Null argument"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    return ctx->commandRegistry->HasCommand(name) ? 1 : 0;
}

int mcp_handle_request(McpServerHandle handle, const char* jsonBody,
                        const char* clientIp, char* responseBuffer, int bufferSize) {
    if (!handle || !jsonBody) { SetError("Null argument"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    std::string ip = clientIp ? clientIp : "0.0.0.0";
    std::string result = ctx->protocolHandler->HandleRequest(jsonBody, ip);
    return CopyToBuffer(result, responseBuffer, bufferSize);
}

int mcp_llm_complete(McpServerHandle handle, const char* requestJson,
                      char* responseBuffer, int bufferSize) {
    if (!handle || !requestJson) { SetError("Null argument"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    try {
        auto req = nlohmann::json::parse(requestJson);
        nlohmann::json fullReq = {{"command", "llm"}, {"payload", req}};
        auto cmd = ctx->commandRegistry->Resolve("llm");
        if (!cmd) { SetError("LLM command not registered"); return -1; }
        auto future = cmd->ExecuteAsync(fullReq);
        auto result = future.get();
        return CopyToBuffer(result.dump(), responseBuffer, bufferSize);
    } catch (const std::exception& e) {
        SetError(e.what());
        return -1;
    }
}

int mcp_skill_list(McpServerHandle handle, char* buffer, int bufferSize) {
    if (!handle) { SetError("Null handle"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    return CopyToBuffer(ctx->skillEngine->ListSkillsJson().dump(), buffer, bufferSize);
}

int mcp_skill_execute(McpServerHandle handle, const char* requestJson,
                       char* responseBuffer, int bufferSize) {
    if (!handle || !requestJson) { SetError("Null argument"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    try {
        auto req = nlohmann::json::parse(requestJson);
        nlohmann::json fullReq = {{"command", "skill"}, {"payload", req}};
        auto cmd = ctx->commandRegistry->Resolve("skill");
        if (!cmd) { SetError("Skill command not registered"); return -1; }
        auto future = cmd->ExecuteAsync(fullReq);
        auto result = future.get();
        return CopyToBuffer(result.dump(), responseBuffer, bufferSize);
    } catch (const std::exception& e) {
        SetError(e.what());
        return -1;
    }
}

int mcp_server_list_remote(McpServerHandle handle, char* buffer, int bufferSize) {
    if (!handle) { SetError("Null handle"); return -1; }
    auto ctx = static_cast<McpServerContext*>(handle);
    return CopyToBuffer(ctx->mcpRegistry->ToJson().dump(), buffer, bufferSize);
}

const char* mcp_last_error(void) {
    return g_LastError.c_str();
}

} // extern "C"
