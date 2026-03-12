#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <cstdlib>
#include <fstream>

class Config {
public:
    static Config loadFromFile(const std::string& path);
    static Config loadFromEnv();

    int serverPort() const;
    std::string litellmBaseUrl() const;
    std::string defaultModel() const;
    size_t threadPoolSize() const;
    size_t maxRequestBodySize() const;
    size_t rateLimitRequestsPerMinute() const;
    bool authEnabled() const;
    std::string authApiKey() const;
    std::string skillsDirectory() const;
    std::string mcpServersConfigPath() const;
    const nlohmann::json& raw() const;

private:
    nlohmann::json data_;
};
