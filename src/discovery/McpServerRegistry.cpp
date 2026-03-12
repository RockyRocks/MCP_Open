#include "discovery/McpServerRegistry.h"
#include "core/Logger.h"
#include <fstream>
#include <algorithm>
#include <regex>
#include <stdexcept>

bool McpServerRegistry::isValidUrl(const std::string& url) {
    std::regex urlRegex(R"(^https?://[a-zA-Z0-9.\-]+(:\d+)?(/.*)?$)");
    return std::regex_match(url, urlRegex);
}

McpServerRegistry McpServerRegistry::loadFromFile(const std::string& path) {
    McpServerRegistry registry;

    std::ifstream file(path);
    if (!file.is_open()) {
        Logger::getInstance().log("MCP servers config not found: " + path + " (using empty registry)");
        return registry;
    }

    auto data = nlohmann::json::parse(file);
    if (!data.contains("servers") || !data["servers"].is_array()) {
        throw std::runtime_error("Invalid mcp_servers.json: missing 'servers' array");
    }

    for (const auto& s : data["servers"]) {
        McpServerEntry entry;
        entry.name = s.value("name", "");
        entry.url = s.value("url", "");
        entry.priority = s.value("priority", 1);
        entry.timeoutSeconds = s.value("timeout_seconds", 30);

        if (!isValidUrl(entry.url)) {
            Logger::getInstance().log("Skipping server with invalid URL: " + entry.url);
            continue;
        }

        if (s.contains("capabilities") && s["capabilities"].is_array()) {
            for (const auto& cap : s["capabilities"]) {
                entry.capabilities.push_back(cap.get<std::string>());
            }
        }

        registry.servers_.push_back(std::move(entry));
    }

    Logger::getInstance().log("Loaded " + std::to_string(registry.servers_.size()) + " MCP servers");
    return registry;
}

void McpServerRegistry::addServer(const McpServerEntry& entry) {
    if (!isValidUrl(entry.url)) {
        throw std::invalid_argument("Invalid server URL: " + entry.url);
    }
    servers_.push_back(entry);
}

std::vector<McpServerEntry> McpServerRegistry::allServers() const {
    return servers_;
}

std::vector<McpServerEntry> McpServerRegistry::serversForCapability(const std::string& capability) const {
    std::vector<McpServerEntry> result;
    for (const auto& server : servers_) {
        for (const auto& cap : server.capabilities) {
            if (cap == capability) {
                result.push_back(server);
                break;
            }
        }
    }
    std::sort(result.begin(), result.end(),
              [](const McpServerEntry& a, const McpServerEntry& b) {
                  return a.priority < b.priority;
              });
    return result;
}

std::optional<McpServerEntry> McpServerRegistry::bestServerFor(const std::string& capability) const {
    auto servers = serversForCapability(capability);
    if (servers.empty()) return std::nullopt;
    return servers.front();
}

bool McpServerRegistry::healthCheck(const McpServerEntry& server, IHttpClient& client) const {
    try {
        auto resp = client.get(server.url + "/health", {}, 5);
        return resp.statusCode == 200;
    } catch (...) {
        return false;
    }
}

nlohmann::json McpServerRegistry::toJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : servers_) {
        arr.push_back({
            {"name", s.name},
            {"url", s.url},
            {"capabilities", s.capabilities},
            {"priority", s.priority}
        });
    }
    return arr;
}
