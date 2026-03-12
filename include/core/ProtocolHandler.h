#pragma once
#include "core/IRequestHandler.h"
#include "commands/CommandRegistry.h"
#include "validation/InputSanitizer.h"
#include "security/RateLimiter.h"
#include "security/ApiKeyValidator.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

class ProtocolHandler : public IRequestHandler {
public:
    ProtocolHandler(std::shared_ptr<CommandRegistry> registry,
                    std::shared_ptr<RateLimiter> rateLimiter,
                    std::shared_ptr<ApiKeyValidator> apiKeyValidator,
                    size_t maxBodySize = 1048576);

    // IRequestHandler interface
    nlohmann::json handle(const nlohmann::json& request) override;

    // Full request handling with security checks
    std::string handleRequest(const std::string& body, const std::string& clientIp,
                               const std::string& authHeader = "");

    bool validateRequest(const nlohmann::json& req);
    std::string createResponse(const nlohmann::json& data);

private:
    std::shared_ptr<CommandRegistry> registry_;
    std::shared_ptr<RateLimiter> rateLimiter_;
    std::shared_ptr<ApiKeyValidator> apiKeyValidator_;
    size_t maxBodySize_;
};
