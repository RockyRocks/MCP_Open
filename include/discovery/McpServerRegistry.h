#pragma once
#include <discovery/McpServerEntry.h>
#include <http/IHttpClient.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

class McpServerRegistry {
public:
    static McpServerRegistry LoadFromFile(const std::string& path);
    void AddServer(const McpServerEntry& entry);
    std::vector<McpServerEntry> GetAllServers() const;
    std::vector<McpServerEntry> GetServersForCapability(const std::string& capability) const;
    std::optional<McpServerEntry> GetBestServerFor(const std::string& capability) const;
    bool HealthCheck(const McpServerEntry& server, IHttpClient& client) const;
    nlohmann::json ToJson() const;

private:
    std::vector<McpServerEntry> m_Servers;
    static bool IsValidUrl(const std::string& url);
};
