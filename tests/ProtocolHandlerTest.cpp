#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "core/ProtocolHandler.h"
#include "commands/CommandRegistry.h"
#include "commands/EchoCommand.h"
#include "security/RateLimiter.h"
#include "security/ApiKeyValidator.h"

class ProtocolHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry = std::make_shared<CommandRegistry>();
        registry->registerCommand("echo", createEchoCommand());
        rateLimiter = std::make_shared<RateLimiter>(60);
        apiKeyValidator = std::make_shared<ApiKeyValidator>("", false);
        handler = std::make_unique<ProtocolHandler>(
            registry, rateLimiter, apiKeyValidator);
    }

    std::shared_ptr<CommandRegistry> registry;
    std::shared_ptr<RateLimiter> rateLimiter;
    std::shared_ptr<ApiKeyValidator> apiKeyValidator;
    std::unique_ptr<ProtocolHandler> handler;
};

TEST_F(ProtocolHandlerTest, ValidRequest) {
    nlohmann::json req = {{"command", "echo"}, {"payload", nlohmann::json::object()}};
    EXPECT_TRUE(handler->validateRequest(req));
}

TEST_F(ProtocolHandlerTest, InvalidRequest) {
    nlohmann::json req = {{"payload", nlohmann::json::object()}};
    EXPECT_FALSE(handler->validateRequest(req));
}

TEST_F(ProtocolHandlerTest, ValidResponse) {
    nlohmann::json res = {{"status", "ok"}};
    std::string s = handler->createResponse(res);
    EXPECT_NE(s.find("ok"), std::string::npos);
}

TEST_F(ProtocolHandlerTest, InvalidResponse) {
    nlohmann::json res = {{"msg", "bad"}};
    std::string s = handler->createResponse(res);
    EXPECT_NE(s.find("error"), std::string::npos);
}

TEST_F(ProtocolHandlerTest, HandleEchoCommand) {
    nlohmann::json req = {{"command", "echo"}, {"payload", {{"msg", "hello"}}}};
    auto result = handler->handle(req);
    EXPECT_EQ(result["status"], "ok");
    EXPECT_TRUE(result.contains("echo"));
}

TEST_F(ProtocolHandlerTest, UnknownCommand) {
    nlohmann::json req = {{"command", "nonexistent"}, {"payload", nlohmann::json::object()}};
    auto result = handler->handle(req);
    EXPECT_EQ(result["status"], "error");
    EXPECT_TRUE(result.contains("error"));
}

TEST_F(ProtocolHandlerTest, HandleRequestFullPipeline) {
    std::string body = R"({"command":"echo","payload":{"msg":"test"}})";
    std::string result = handler->handleRequest(body, "127.0.0.1");
    auto parsed = nlohmann::json::parse(result);
    EXPECT_EQ(parsed["status"], "ok");
}

TEST_F(ProtocolHandlerTest, HandleRequestInvalidJson) {
    std::string result = handler->handleRequest("not json", "127.0.0.1");
    EXPECT_NE(result.find("Invalid JSON"), std::string::npos);
}

TEST_F(ProtocolHandlerTest, HandleRequestPayloadTooLarge) {
    auto smallHandler = std::make_unique<ProtocolHandler>(
        registry, rateLimiter, apiKeyValidator, 10);
    std::string body = R"({"command":"echo","payload":{"msg":"this is way too long"}})";
    std::string result = smallHandler->handleRequest(body, "127.0.0.1");
    EXPECT_NE(result.find("Payload too large"), std::string::npos);
}
