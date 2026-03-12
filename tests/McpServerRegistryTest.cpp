#include <gtest/gtest.h>
#include "discovery/McpServerRegistry.h"
#include <fstream>

TEST(McpServerRegistryTest, LoadFromFile) {
    std::string path = "test_servers_tmp.json";
    {
        std::ofstream f(path);
        f << R"({
            "servers": [
                {
                    "name": "server-a",
                    "url": "http://localhost:9002",
                    "capabilities": ["analyze", "summarize"],
                    "priority": 1
                },
                {
                    "name": "server-b",
                    "url": "http://localhost:9003",
                    "capabilities": ["code_review"],
                    "priority": 2
                }
            ]
        })";
    }

    auto reg = McpServerRegistry::loadFromFile(path);
    EXPECT_EQ(reg.allServers().size(), 2u);
    std::remove(path.c_str());
}

TEST(McpServerRegistryTest, LookupByCapability) {
    McpServerRegistry reg;
    McpServerEntry a;
    a.name = "server-a";
    a.url = "http://localhost:9002";
    a.capabilities = {"analyze", "summarize"};
    a.priority = 1;
    reg.addServer(a);

    auto servers = reg.serversForCapability("analyze");
    EXPECT_EQ(servers.size(), 1u);
    EXPECT_EQ(servers[0].name, "server-a");

    auto none = reg.serversForCapability("nonexistent");
    EXPECT_TRUE(none.empty());
}

TEST(McpServerRegistryTest, BestServerByPriority) {
    McpServerRegistry reg;

    McpServerEntry a;
    a.name = "low-priority";
    a.url = "http://localhost:9002";
    a.capabilities = {"analyze"};
    a.priority = 10;
    reg.addServer(a);

    McpServerEntry b;
    b.name = "high-priority";
    b.url = "http://localhost:9003";
    b.capabilities = {"analyze"};
    b.priority = 1;
    reg.addServer(b);

    auto best = reg.bestServerFor("analyze");
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->name, "high-priority");
}

TEST(McpServerRegistryTest, InvalidUrlRejected) {
    McpServerRegistry reg;
    McpServerEntry bad;
    bad.name = "bad";
    bad.url = "not_a_url";
    bad.capabilities = {"test"};
    EXPECT_THROW(reg.addServer(bad), std::invalid_argument);
}

TEST(McpServerRegistryTest, MissingFileReturnsEmpty) {
    auto reg = McpServerRegistry::loadFromFile("nonexistent_servers.json");
    EXPECT_TRUE(reg.allServers().empty());
}
