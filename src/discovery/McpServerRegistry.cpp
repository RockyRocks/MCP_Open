#include <discovery/McpServerRegistry.h>
#include <core/Logger.h>
#include <fstream>
#include <algorithm>
#include <regex>
#include <stdexcept>

bool McpServerRegistry::IsValidUrl(const std::string& url) {
    std::regex urlRegex(R"(^https?://[a-zA-Z0-9.\-]+(:\d+)?(/.*)?$)");
    return std::regex_match(url, urlRegex);
}

McpServerRegistry McpServerRegistry::LoadFromFile(const std::string& path) {
    McpServerRegistry registry;

    std::ifstream file(path);
    if (!file.is_open()) {
        Logger::GetInstance().Log("MCP servers config not found: " + path + " (using empty registry)");
        return registry;
    }

    auto data = nlohmann::json::parse(file);
    if (!data.contains("servers") || !data["servers"].is_array()) {
        throw std::runtime_error("Invalid mcp_servers.json: missing 'servers' array");
    }

    for (const auto& s : data["servers"]) {
        McpServerEntry entry;
        entry.m_Name = s.value("name", "");
        entry.m_Url = s.value("url", "");
        entry.m_Priority = s.value("priority", 1);
        entry.m_TimeoutSeconds = s.value("timeout_seconds", 30);

        if (!IsValidUrl(entry.m_Url)) {
            Logger::GetInstance().Log("Skipping server with invalid URL: " + entry.m_Url);
            continue;
        }

        if (s.contains("capabilities") && s["capabilities"].is_array()) {
            for (const auto& cap : s["capabilities"]) {
                entry.m_Capabilities.push_back(cap.get<std::string>());
            }
        }

        registry.m_Servers.push_back(std::move(entry));
    }

    Logger::GetInstance().Log("Loaded " + std::to_string(registry.m_Servers.size()) + " MCP servers");
    return registry;
}

void McpServerRegistry::AddServer(const McpServerEntry& entry) {
    if (!IsValidUrl(entry.m_Url)) {
        throw std::invalid_argument("Invalid server URL: " + entry.m_Url);
    }
    m_Servers.push_back(entry);
}

std::vector<McpServerEntry> McpServerRegistry::GetAllServers() const {
    return m_Servers;
}

std::vector<McpServerEntry> McpServerRegistry::GetServersForCapability(const std::string& capability) const {
    std::vector<McpServerEntry> result;
    for (const auto& server : m_Servers) {
        for (const auto& cap : server.m_Capabilities) {
            if (cap == capability) {
                result.push_back(server);
                break;
            }
        }
    }
    std::sort(result.begin(), result.end(),
              [](const McpServerEntry& a, const McpServerEntry& b) {
                  return a.m_Priority < b.m_Priority;
              });
    return result;
}

std::optional<McpServerEntry> McpServerRegistry::GetBestServerFor(const std::string& capability) const {
    auto servers = GetServersForCapability(capability);
    if (servers.empty()) return std::nullopt;
    return servers.front();
}

bool McpServerRegistry::HealthCheck(const McpServerEntry& server, IHttpClient& client) const {
    try {
        auto resp = client.Get(server.m_Url + "/health", {}, 5);
        return resp.m_StatusCode == 200;
    } catch (...) {
        return false;
    }
}

nlohmann::json McpServerRegistry::ToJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : m_Servers) {
        arr.push_back({
            {"name", s.m_Name},
            {"url", s.m_Url},
            {"capabilities", s.m_Capabilities},
            {"priority", s.m_Priority}
        });
    }
    return arr;
}
