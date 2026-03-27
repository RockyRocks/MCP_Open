#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <cstdlib>
#include <fstream>

class Config {
public:
    static Config LoadFromFile(const std::string& path);
    static Config LoadFromEnv();

    int GetServerPort() const;
    std::string GetLiteLlmBaseUrl() const;
    std::string GetDefaultModel() const;
    size_t GetThreadPoolSize() const;
    size_t GetMaxRequestBodySize() const;
    size_t GetRateLimitRequestsPerMinute() const;
    bool IsAuthEnabled() const;
    std::string GetAuthApiKey() const;
    std::string GetSkillsDirectory() const;
    std::string GetMcpServersConfigPath() const;
    const nlohmann::json& GetRaw() const;

private:
    nlohmann::json m_Data;
};
