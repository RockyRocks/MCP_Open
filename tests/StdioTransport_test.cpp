#include <gtest/gtest.h>
#include <server/StdioTransport.h>
#include <commands/CommandRegistry.h>
#include <commands/EchoCommand.h>
#include <skills/SkillEngine.h>
#include <discovery/McpServerRegistry.h>

#include <sstream>

namespace {

// Helper: create a transport with canned input, capture output
struct StdioTestFixture {
    std::shared_ptr<CommandRegistry> registry;
    std::shared_ptr<SkillEngine> skillEngine;
    std::shared_ptr<McpServerRegistry> mcpRegistry;

    StdioTestFixture() {
        registry = std::make_shared<CommandRegistry>();
        registry->RegisterCommand("echo", CreateEchoCommand());
        skillEngine = std::make_shared<SkillEngine>();
        mcpRegistry = std::make_shared<McpServerRegistry>();
    }

    // Send one or more JSON-RPC messages, return all response lines
    std::vector<nlohmann::json> run(const std::vector<std::string>& inputLines) {
        std::string inputStr;
        for (const auto& line : inputLines) {
            inputStr += line + "\n";
        }

        std::istringstream input(inputStr);
        std::ostringstream output;

        StdioTransport transport(registry, skillEngine, mcpRegistry,
                                 input, output);
        transport.Run();

        // Parse output lines
        std::vector<nlohmann::json> responses;
        std::istringstream outStream(output.str());
        std::string line;
        while (std::getline(outStream, line)) {
            if (!line.empty()) {
                responses.push_back(nlohmann::json::parse(line));
            }
        }
        return responses;
    }

    // Convenience: send single message, return single response
    nlohmann::json sendOne(const std::string& input) {
        auto responses = run({input});
        EXPECT_FALSE(responses.empty());
        return responses.empty() ? nlohmann::json() : responses[0];
    }
};

std::string makeRequest(const std::string& method, int id,
                        const nlohmann::json& params = nlohmann::json::object()) {
    nlohmann::json req = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", params}
    };
    return req.dump();
}

std::string makeNotification(const std::string& method,
                             const nlohmann::json& params = nlohmann::json::object()) {
    nlohmann::json req = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params}
    };
    return req.dump();
}

} // anonymous namespace

TEST(StdioTransportTest, InitializeReturnsCapabilities) {
    StdioTestFixture f;
    nlohmann::json params = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"clientInfo", {{"name", "test"}, {"version", "1.0"}}}
    };

    auto resp = f.sendOne(makeRequest("initialize", 1, params));

    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], 1);
    EXPECT_TRUE(resp.contains("result"));

    auto result = resp["result"];
    EXPECT_EQ(result["protocolVersion"], "2024-11-05");
    EXPECT_TRUE(result["capabilities"].contains("tools"));
    EXPECT_TRUE(result["capabilities"].contains("prompts"));
    EXPECT_EQ(result["serverInfo"]["name"], "mcp-open");
}

TEST(StdioTransportTest, ToolsListReturnsRegisteredTools) {
    StdioTestFixture f;
    auto responses = f.run({
        makeRequest("initialize", 1),
        makeNotification("notifications/initialized"),
        makeRequest("tools/list", 2)
    });

    // Should have 2 responses (initialize + tools/list; notification has none)
    ASSERT_GE(responses.size(), 2u);

    auto toolsResp = responses[1];
    EXPECT_EQ(toolsResp["id"], 2);
    EXPECT_TRUE(toolsResp["result"].contains("tools"));

    auto tools = toolsResp["result"]["tools"];
    EXPECT_EQ(tools.size(), 1u); // only echo registered in fixture

    auto echo = tools[0];
    EXPECT_EQ(echo["name"], "echo");
    EXPECT_TRUE(echo.contains("inputSchema"));
}

TEST(StdioTransportTest, ToolsCallEchoReturnsContent) {
    StdioTestFixture f;
    nlohmann::json callParams = {
        {"name", "echo"},
        {"arguments", {{"message", "hello world"}}}
    };

    auto responses = f.run({
        makeRequest("initialize", 1),
        makeNotification("notifications/initialized"),
        makeRequest("tools/call", 2, callParams)
    });

    ASSERT_GE(responses.size(), 2u);
    auto callResp = responses[1];
    EXPECT_EQ(callResp["id"], 2);
    EXPECT_TRUE(callResp["result"].contains("content"));
    EXPECT_FALSE(callResp["result"]["isError"].get<bool>());
}

TEST(StdioTransportTest, ToolsCallUnknownToolReturnsError) {
    StdioTestFixture f;
    nlohmann::json callParams = {
        {"name", "nonexistent"},
        {"arguments", nlohmann::json::object()}
    };

    auto responses = f.run({
        makeRequest("initialize", 1),
        makeNotification("notifications/initialized"),
        makeRequest("tools/call", 2, callParams)
    });

    ASSERT_GE(responses.size(), 2u);
    auto callResp = responses[1];
    EXPECT_TRUE(callResp.contains("error"));
    EXPECT_EQ(callResp["error"]["code"], -32602);
}

TEST(StdioTransportTest, PromptsListReturnsEmpty) {
    StdioTestFixture f;
    auto responses = f.run({
        makeRequest("initialize", 1),
        makeNotification("notifications/initialized"),
        makeRequest("prompts/list", 2)
    });

    ASSERT_GE(responses.size(), 2u);
    auto promptsResp = responses[1];
    EXPECT_TRUE(promptsResp["result"].contains("prompts"));
    EXPECT_EQ(promptsResp["result"]["prompts"].size(), 0u);
}

TEST(StdioTransportTest, InvalidJsonReturnsParseError) {
    StdioTestFixture f;
    auto resp = f.sendOne("this is not json");

    EXPECT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32700);
}

TEST(StdioTransportTest, UnknownMethodReturnsMethodNotFound) {
    StdioTestFixture f;
    auto responses = f.run({
        makeRequest("initialize", 1),
        makeNotification("notifications/initialized"),
        makeRequest("unknown/method", 2)
    });

    ASSERT_GE(responses.size(), 2u);
    auto resp = responses[1];
    EXPECT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32601);
}

TEST(StdioTransportTest, MethodBeforeInitializeReturnsError) {
    StdioTestFixture f;
    auto resp = f.sendOne(makeRequest("tools/list", 1));

    EXPECT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32600);
}

TEST(StdioTransportTest, ToolsListUsesCommandMetadata) {
    StdioTestFixture f;
    auto responses = f.run({
        makeRequest("initialize", 1),
        makeNotification("notifications/initialized"),
        makeRequest("tools/list", 2)
    });

    ASSERT_GE(responses.size(), 2u);
    auto toolsResp = responses[1];
    auto tools = toolsResp["result"]["tools"];

    // Find the echo tool
    bool foundEcho = false;
    for (const auto& tool : tools) {
        if (tool["name"] == "echo") {
            foundEcho = true;
            EXPECT_EQ(tool["description"], "Echo back the input message");
            EXPECT_TRUE(tool["inputSchema"]["properties"].contains("message"));
            break;
        }
    }
    EXPECT_TRUE(foundEcho);
}

// Stub command that captures the request it receives, with a default model
namespace {
class CapturingCommand : public ICommandStrategy {
public:
    mutable nlohmann::json lastRequest;

    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) override {
        lastRequest = request;
        return std::async(std::launch::async, [request]() {
            return nlohmann::json{{"status", "ok"}};
        });
    }

    ToolMetadata GetMetadata() const override {
        return {
            "capturing",
            "A test command that captures requests",
            {
                {"type", "object"},
                {"properties", {
                    {"input", {{"type", "string"}}}
                }}
            },
            "claude-opus",
            {{"temperature", 0.1}}
        };
    }
};
} // anonymous namespace

TEST(StdioTransportTest, HiddenToolsExcludedFromToolsList) {
    // Register a hidden command alongside echo — it must not appear in tools/list
    auto registry = std::make_shared<CommandRegistry>();
    registry->RegisterCommand("echo", CreateEchoCommand());

    class HiddenCmd : public ICommandStrategy {
    public:
        std::future<nlohmann::json> ExecuteAsync(const nlohmann::json&) override {
            return std::async(std::launch::async, []() {
                return nlohmann::json{{"status", "ok"}};
            });
        }
        ToolMetadata GetMetadata() const override {
            ToolMetadata m;
            m.m_Name = "secret";
            m.m_Hidden = true;
            return m;
        }
    };
    registry->RegisterCommand("secret", std::make_shared<HiddenCmd>());

    auto skillEngine = std::make_shared<SkillEngine>();
    auto mcpRegistry = std::make_shared<McpServerRegistry>();

    std::string inputStr;
    inputStr += makeRequest("initialize", 1) + "\n";
    inputStr += makeNotification("notifications/initialized") + "\n";
    inputStr += makeRequest("tools/list", 2) + "\n";

    std::istringstream input(inputStr);
    std::ostringstream output;
    StdioTransport transport(registry, skillEngine, mcpRegistry, input, output);
    transport.Run();

    std::istringstream out(output.str());
    std::vector<nlohmann::json> responses;
    std::string line;
    while (std::getline(out, line)) {
        if (!line.empty()) responses.push_back(nlohmann::json::parse(line));
    }

    ASSERT_GE(responses.size(), 2u);
    auto tools = responses[1]["result"]["tools"];
    for (const auto& t : tools) {
        EXPECT_NE(t["name"], "secret");
    }
}

TEST(StdioTransportTest, DefaultModelInjectedInToolsCall) {
    auto registry = std::make_shared<CommandRegistry>();
    auto capCmd = std::make_shared<CapturingCommand>();
    registry->RegisterCommand("capturing", capCmd);

    auto skillEngine = std::make_shared<SkillEngine>();
    auto mcpRegistry = std::make_shared<McpServerRegistry>();

    nlohmann::json callParams = {
        {"name", "capturing"},
        {"arguments", {{"input", "hello"}}}
    };

    // Build request sequence: initialize + notify + tools/call
    std::string inputStr;
    inputStr += makeRequest("initialize", 1) + "\n";
    inputStr += makeNotification("notifications/initialized") + "\n";
    inputStr += makeRequest("tools/call", 2, callParams) + "\n";

    std::istringstream input(inputStr);
    std::ostringstream output;

    StdioTransport transport(registry, skillEngine, mcpRegistry, input, output);
    transport.Run();

    // Verify the captured request has the default model injected
    ASSERT_FALSE(capCmd->lastRequest.is_null());
    EXPECT_EQ(capCmd->lastRequest["payload"]["model"], "claude-opus");
    EXPECT_EQ(capCmd->lastRequest["payload"]["parameters"]["temperature"], 0.1);
}

// ---------------------------------------------------------------------------
// tools/call_batch tests
// ---------------------------------------------------------------------------

TEST(StdioTransportTest, CallBatchExecutesMultipleCalls) {
    StdioTestFixture f;

    nlohmann::json batchParams = {
        {"calls", nlohmann::json::array({
            {{"name", "echo"}, {"arguments", {{"message", "hello"}}}},
            {{"name", "echo"}, {"arguments", {{"message", "world"}}}}
        })}
    };

    auto responses = f.run({
        makeRequest("initialize", 1),
        makeNotification("notifications/initialized"),
        makeRequest("tools/call_batch", 2, batchParams)
    });

    ASSERT_GE(responses.size(), 2u);
    auto batchResp = responses[1];
    EXPECT_EQ(batchResp["id"], 2);
    ASSERT_TRUE(batchResp["result"].contains("results"));
    EXPECT_EQ(batchResp["result"]["results"].size(), 2u);
}

TEST(StdioTransportTest, CallBatchAllSucceed) {
    StdioTestFixture f;

    nlohmann::json batchParams = {
        {"calls", nlohmann::json::array({
            {{"name", "echo"}, {"arguments", {{"message", "a"}}}},
            {{"name", "echo"}, {"arguments", {{"message", "b"}}}}
        })}
    };

    auto responses = f.run({
        makeRequest("initialize", 1),
        makeNotification("notifications/initialized"),
        makeRequest("tools/call_batch", 2, batchParams)
    });

    auto results = responses[1]["result"]["results"];
    for (const auto& r : results) {
        EXPECT_FALSE(r["isError"].get<bool>());
        EXPECT_EQ(r["name"], "echo");
    }
}

TEST(StdioTransportTest, CallBatchUnknownToolMarkedAsError) {
    StdioTestFixture f;

    nlohmann::json batchParams = {
        {"calls", nlohmann::json::array({
            {{"name", "echo"}, {"arguments", {{"message", "ok"}}}},
            {{"name", "no_such_tool"}, {"arguments", nlohmann::json::object()}}
        })}
    };

    auto responses = f.run({
        makeRequest("initialize", 1),
        makeNotification("notifications/initialized"),
        makeRequest("tools/call_batch", 2, batchParams)
    });

    auto results = responses[1]["result"]["results"];
    ASSERT_EQ(results.size(), 2u);

    // echo succeeded
    bool foundEcho = false, foundError = false;
    for (const auto& r : results) {
        if (r["name"] == "echo") {
            foundEcho = true;
            EXPECT_FALSE(r["isError"].get<bool>());
        }
        if (r["name"] == "no_such_tool") {
            foundError = true;
            EXPECT_TRUE(r["isError"].get<bool>());
        }
    }
    EXPECT_TRUE(foundEcho);
    EXPECT_TRUE(foundError);
}

TEST(StdioTransportTest, CallBatchEmptyCallsReturnsEmptyResults) {
    StdioTestFixture f;

    nlohmann::json batchParams = {{"calls", nlohmann::json::array()}};

    auto responses = f.run({
        makeRequest("initialize", 1),
        makeNotification("notifications/initialized"),
        makeRequest("tools/call_batch", 2, batchParams)
    });

    ASSERT_GE(responses.size(), 2u);
    auto results = responses[1]["result"]["results"];
    EXPECT_EQ(results.size(), 0u);
}

TEST(StdioTransportTest, CallBatchMissingCallsParamReturnsError) {
    StdioTestFixture f;

    nlohmann::json batchParams = nlohmann::json::object(); // no "calls"

    auto responses = f.run({
        makeRequest("initialize", 1),
        makeNotification("notifications/initialized"),
        makeRequest("tools/call_batch", 2, batchParams)
    });

    ASSERT_GE(responses.size(), 2u);
    EXPECT_TRUE(responses[1].contains("error"));
    EXPECT_EQ(responses[1]["error"]["code"], -32602);
}

TEST(StdioTransportTest, CallBatchCallMissingNameFieldMarkedAsError) {
    StdioTestFixture f;

    nlohmann::json batchParams = {
        {"calls", nlohmann::json::array({
            {{"arguments", {{"message", "no name field"}}}} // missing "name"
        })}
    };

    auto responses = f.run({
        makeRequest("initialize", 1),
        makeNotification("notifications/initialized"),
        makeRequest("tools/call_batch", 2, batchParams)
    });

    auto results = responses[1]["result"]["results"];
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0]["isError"].get<bool>());
}
