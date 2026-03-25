#include <gtest/gtest.h>
#include "server/StdioTransport.h"
#include "commands/CommandRegistry.h"
#include "commands/EchoCommand.h"
#include "skills/SkillEngine.h"
#include "discovery/McpServerRegistry.h"

#include <sstream>

namespace {

// Helper: create a transport with canned input, capture output
struct StdioTestFixture {
    std::shared_ptr<CommandRegistry> registry;
    std::shared_ptr<SkillEngine> skillEngine;
    std::shared_ptr<McpServerRegistry> mcpRegistry;

    StdioTestFixture() {
        registry = std::make_shared<CommandRegistry>();
        registry->registerCommand("echo", createEchoCommand());
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
        transport.run();

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
