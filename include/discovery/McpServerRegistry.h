#pragma once
#include "discovery/McpServerEntry.h"
#include "http/IHttpClient.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

class McpServerRegistry {
public:
    static McpServerRegistry loadFromFile(const std::string& path);
    void addServer(const McpServerEntry& entry);
    std::vector<McpServerEntry> allServers() const;
    std::vector<McpServerEntry> serversForCapability(const std::string& capability) const;
    std::optional<McpServerEntry> bestServerFor(const std::string& capability) const;
    bool healthCheck(const McpServerEntry& server, IHttpClient& client) const;
    nlohmann::json toJson() const;

private:
    std::vector<McpServerEntry> servers_;
    static bool isValidUrl(const std::string& url);
};
