#include <core/ProtocolHandler.h>
#include <validation/JsonSchemaValidator.h>
#include <core/Logger.h>
#include <nlohmann/json.hpp>

static const nlohmann::json requestSchema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "properties": {
    "command": {"type": "string"},
    "payload": {"type": "object"}
  },
  "required": ["command"]
})"_json;

static const nlohmann::json responseSchema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "properties": {
    "status": {"type": "string"}
  },
  "required": ["status"]
})"_json;

ProtocolHandler::ProtocolHandler(std::shared_ptr<CommandRegistry> registry,
                                   std::shared_ptr<RateLimiter> rateLimiter,
                                   std::shared_ptr<ApiKeyValidator> apiKeyValidator,
                                   size_t maxBodySize)
    : m_Registry(std::move(registry))
    , m_RateLimiter(std::move(rateLimiter))
    , m_ApiKeyValidator(std::move(apiKeyValidator))
    , m_MaxBodySize(maxBodySize) {}

nlohmann::json ProtocolHandler::Handle(const nlohmann::json& request) {
    if (!ValidateRequest(request)) {
        return {{"status", "error"}, {"error", "Invalid request schema"}};
    }

    std::string command = request["command"].get<std::string>();
    auto cmd = m_Registry->Resolve(command);
    if (!cmd) {
        return {{"status", "error"},
                {"error", "Unknown command: " + command},
                {"available_commands", m_Registry->ListCommands()}};
    }

    try {
        auto future = cmd->ExecuteAsync(request);
        return future.get();
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(std::string("Command execution failed: ") + e.what());
        return {{"status", "error"}, {"error", "Command execution failed"}};
    }
}

std::string ProtocolHandler::HandleRequest(const std::string& body,
                                             const std::string& clientIp,
                                             const std::string& authHeader) {
    // Security: Check payload size
    if (!InputSanitizer::ValidatePayloadSize(body, m_MaxBodySize)) {
        return R"({"status":"error","error":"Payload too large"})";
    }

    // Security: Rate limiting
    if (m_RateLimiter && !m_RateLimiter->AllowRequest(clientIp)) {
        return R"({"status":"error","error":"Rate limit exceeded"})";
    }

    // Security: API key validation
    if (m_ApiKeyValidator && m_ApiKeyValidator->IsEnabled()) {
        if (!m_ApiKeyValidator->Validate(authHeader)) {
            return R"({"status":"error","error":"Unauthorized"})";
        }
    }

    // Parse JSON
    nlohmann::json req;
    try {
        req = nlohmann::json::parse(body);
    } catch (const std::exception&) {
        return R"({"status":"error","error":"Invalid JSON"})";
    }

    // Security: Check JSON depth
    if (!InputSanitizer::ValidateJsonDepth(req)) {
        return R"({"status":"error","error":"JSON nesting too deep"})";
    }

    // Handle the request
    auto result = Handle(req);
    return CreateResponse(result);
}

bool ProtocolHandler::ValidateRequest(const nlohmann::json& req) {
    JsonSchemaValidator v(requestSchema);
    if (!v.Validate(req)) {
        Logger::GetInstance().Log(std::string("Request validation failed: ") + v.GetErrorMessage());
        return false;
    }
    return true;
}

std::string ProtocolHandler::CreateResponse(const nlohmann::json& data) {
    JsonSchemaValidator v(responseSchema);
    if (!v.Validate(data)) {
        Logger::GetInstance().Log(std::string("Response validation failed: ") + v.GetErrorMessage());
        return R"({"status":"error","error":"Invalid response"})";
    }
    return data.dump();
}
