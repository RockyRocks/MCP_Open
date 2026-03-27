#include <gtest/gtest.h>
#include <discovery/McpServerRegistry.h>
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

    auto reg = McpServerRegistry::LoadFromFile(path);
    EXPECT_EQ(reg.GetAllServers().size(), 2u);
    std::remove(path.c_str());
}

TEST(McpServerRegistryTest, LookupByCapability) {
    McpServerRegistry reg;
    McpServerEntry a;
    a.m_Name = "server-a";
    a.m_Url = "http://localhost:9002";
    a.m_Capabilities = {"analyze", "summarize"};
    a.m_Priority = 1;
    reg.AddServer(a);

    auto servers = reg.GetServersForCapability("analyze");
    EXPECT_EQ(servers.size(), 1u);
    EXPECT_EQ(servers[0].m_Name, "server-a");

    auto none = reg.GetServersForCapability("nonexistent");
    EXPECT_TRUE(none.empty());
}

TEST(McpServerRegistryTest, BestServerByPriority) {
    McpServerRegistry reg;

    McpServerEntry a;
    a.m_Name = "low-priority";
    a.m_Url = "http://localhost:9002";
    a.m_Capabilities = {"analyze"};
    a.m_Priority = 10;
    reg.AddServer(a);

    McpServerEntry b;
    b.m_Name = "high-priority";
    b.m_Url = "http://localhost:9003";
    b.m_Capabilities = {"analyze"};
    b.m_Priority = 1;
    reg.AddServer(b);

    auto best = reg.GetBestServerFor("analyze");
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->m_Name, "high-priority");
}

TEST(McpServerRegistryTest, InvalidUrlRejected) {
    McpServerRegistry reg;
    McpServerEntry bad;
    bad.m_Name = "bad";
    bad.m_Url = "not_a_url";
    bad.m_Capabilities = {"test"};
    EXPECT_THROW(reg.AddServer(bad), std::invalid_argument);
}

TEST(McpServerRegistryTest, MissingFileReturnsEmpty) {
    auto reg = McpServerRegistry::LoadFromFile("nonexistent_servers.json");
    EXPECT_TRUE(reg.GetAllServers().empty());
}
