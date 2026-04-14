#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <core/ProtocolHandler.h>
#include <commands/CommandRegistry.h>
#include <commands/EchoCommand.h>
#include <commands/ICommandStrategy.h>
#include <commands/ToolMetadata.h>
#include <security/RateLimiter.h>
#include <security/ApiKeyValidator.h>
#include <future>

class ProtocolHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry = std::make_shared<CommandRegistry>();
        registry->RegisterCommand("echo", CreateEchoCommand());
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
    EXPECT_TRUE(handler->ValidateRequest(req));
}

TEST_F(ProtocolHandlerTest, InvalidRequest) {
    nlohmann::json req = {{"payload", nlohmann::json::object()}};
    EXPECT_FALSE(handler->ValidateRequest(req));
}

TEST_F(ProtocolHandlerTest, ValidResponse) {
    nlohmann::json res = {{"status", "ok"}};
    std::string s = handler->CreateResponse(res);
    EXPECT_NE(s.find("ok"), std::string::npos);
}

TEST_F(ProtocolHandlerTest, InvalidResponse) {
    nlohmann::json res = {{"msg", "bad"}};
    std::string s = handler->CreateResponse(res);
    EXPECT_NE(s.find("error"), std::string::npos);
}

TEST_F(ProtocolHandlerTest, HandleEchoCommand) {
    nlohmann::json req = {{"command", "echo"}, {"payload", {{"msg", "hello"}}}};
    auto result = handler->Handle(req);
    EXPECT_EQ(result["status"], "ok");
    EXPECT_TRUE(result.contains("echo"));
}

TEST_F(ProtocolHandlerTest, UnknownCommand) {
    nlohmann::json req = {{"command", "nonexistent"}, {"payload", nlohmann::json::object()}};
    auto result = handler->Handle(req);
    EXPECT_EQ(result["status"], "error");
    EXPECT_TRUE(result.contains("error"));
}

TEST_F(ProtocolHandlerTest, HandleRequestFullPipeline) {
    std::string body = R"({"command":"echo","payload":{"msg":"test"}})";
    std::string result = handler->HandleRequest(body, "127.0.0.1");
    auto parsed = nlohmann::json::parse(result);
    EXPECT_EQ(parsed["status"], "ok");
}

TEST_F(ProtocolHandlerTest, HandleRequestInvalidJson) {
    std::string result = handler->HandleRequest("not json", "127.0.0.1");
    EXPECT_NE(result.find("Invalid JSON"), std::string::npos);
}

TEST_F(ProtocolHandlerTest, HandleRequestPayloadTooLarge) {
    auto smallHandler = std::make_unique<ProtocolHandler>(
        registry, rateLimiter, apiKeyValidator, 10);
    std::string body = R"({"command":"echo","payload":{"msg":"this is way too long"}})";
    std::string result = smallHandler->HandleRequest(body, "127.0.0.1");
    EXPECT_NE(result.find("Payload too large"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Tool chaining via ProtocolHandler::Handle — uses CommandRegistry::ExecuteWithChaining
// ---------------------------------------------------------------------------

namespace {

/// First command in the chain — returns a result with a "chain" directive.
class PhChainFirstCommand : public ICommandStrategy {
    std::string m_NextTool;
public:
    explicit PhChainFirstCommand(std::string nextTool) : m_NextTool(std::move(nextTool)) {}

    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json&) override {
        return std::async(std::launch::async, [n = m_NextTool]() -> nlohmann::json {
            return {
                {"status", "ok"},
                {"chain",  {{"tool", n}, {"args", nlohmann::json::object()}}}
            };
        });
    }
    ToolMetadata GetMetadata() const override { return {}; }
};

/// Second (final) command in the chain.
class PhChainFinalCommand : public ICommandStrategy {
public:
    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json&) override {
        return std::async(std::launch::async, []() -> nlohmann::json {
            return {{"status", "ok"}, {"chained_result", "from_tool_b"}};
        });
    }
    ToolMetadata GetMetadata() const override { return {}; }
};

} // anonymous namespace

TEST_F(ProtocolHandlerTest, Handle_ToolResponseWithChain_DispatchesNextTool) {
    // tool_a chains to tool_b; Handle() should return tool_b's final result
    registry->RegisterCommand("ph_chain_a",
        std::make_shared<PhChainFirstCommand>("ph_chain_b"));
    registry->RegisterCommand("ph_chain_b",
        std::make_shared<PhChainFinalCommand>());

    nlohmann::json req = {
        {"command", "ph_chain_a"},
        {"payload", nlohmann::json::object()}
    };
    auto result = handler->Handle(req);

    // The chain must have been followed — we expect tool_b's payload
    EXPECT_EQ(result["status"],          "ok");
    EXPECT_EQ(result["chained_result"],  "from_tool_b");
    EXPECT_FALSE(result.contains("chain"));
}
