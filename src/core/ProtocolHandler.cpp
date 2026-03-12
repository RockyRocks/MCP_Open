#include "core/ProtocolHandler.h"
#include "validation/JsonSchemaValidator.h"
#include "core/Logger.h"
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
    : registry_(std::move(registry))
    , rateLimiter_(std::move(rateLimiter))
    , apiKeyValidator_(std::move(apiKeyValidator))
    , maxBodySize_(maxBodySize) {}

nlohmann::json ProtocolHandler::handle(const nlohmann::json& request) {
    if (!validateRequest(request)) {
        return {{"status", "error"}, {"error", "Invalid request schema"}};
    }

    std::string command = request["command"].get<std::string>();
    auto cmd = registry_->resolve(command);
    if (!cmd) {
        return {{"status", "error"},
                {"error", "Unknown command: " + command},
                {"available_commands", registry_->listCommands()}};
    }

    try {
        auto future = cmd->executeAsync(request);
        return future.get();
    } catch (const std::exception& e) {
        Logger::getInstance().log(std::string("Command execution failed: ") + e.what());
        return {{"status", "error"}, {"error", "Command execution failed"}};
    }
}

std::string ProtocolHandler::handleRequest(const std::string& body,
                                             const std::string& clientIp,
                                             const std::string& authHeader) {
    // Security: Check payload size
    if (!InputSanitizer::validatePayloadSize(body, maxBodySize_)) {
        return R"({"status":"error","error":"Payload too large"})";
    }

    // Security: Rate limiting
    if (rateLimiter_ && !rateLimiter_->allowRequest(clientIp)) {
        return R"({"status":"error","error":"Rate limit exceeded"})";
    }

    // Security: API key validation
    if (apiKeyValidator_ && apiKeyValidator_->isEnabled()) {
        if (!apiKeyValidator_->validate(authHeader)) {
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
    if (!InputSanitizer::validateJsonDepth(req)) {
        return R"({"status":"error","error":"JSON nesting too deep"})";
    }

    // Handle the request
    auto result = handle(req);
    return createResponse(result);
}

bool ProtocolHandler::validateRequest(const nlohmann::json& req) {
    JsonSchemaValidator v(requestSchema);
    if (!v.validate(req)) {
        Logger::getInstance().log(std::string("Request validation failed: ") + v.getErrorMessage());
        return false;
    }
    return true;
}

std::string ProtocolHandler::createResponse(const nlohmann::json& data) {
    JsonSchemaValidator v(responseSchema);
    if (!v.validate(data)) {
        Logger::getInstance().log(std::string("Response validation failed: ") + v.getErrorMessage());
        return R"({"status":"error","error":"Invalid response"})";
    }
    return data.dump();
}
