#include <core/Config.h>
#include <fstream>
#include <stdexcept>
#include <thread>

Config Config::LoadFromFile(const std::string& path) {
    Config cfg;
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + path);
    }
    cfg.m_Data = nlohmann::json::parse(file);
    return cfg;
}

Config Config::LoadFromEnv() {
    Config cfg;
    cfg.m_Data = nlohmann::json::object();

    auto getEnv = [](const char* name) -> std::string {
#ifdef _MSC_VER
        char* val = nullptr;
        size_t len = 0;
        if (_dupenv_s(&val, &len, name) == 0 && val != nullptr) {
            std::string result(val);
            free(val);
            return result;
        }
        return "";
#else
        const char* val = std::getenv(name);
        return val ? std::string(val) : "";
#endif
    };

    std::string port = getEnv("MCP_SERVER_PORT");
    if (!port.empty()) cfg.m_Data["server"]["port"] = std::stoi(port);

    std::string litellmUrl = getEnv("MCP_LITELLM_URL");
    if (!litellmUrl.empty()) cfg.m_Data["litellm"]["base_url"] = litellmUrl;

    std::string model = getEnv("MCP_DEFAULT_MODEL");
    if (!model.empty()) cfg.m_Data["litellm"]["default_model"] = model;

    std::string authKey = getEnv("MCP_AUTH_API_KEY");
    if (!authKey.empty()) {
        cfg.m_Data["auth"]["enabled"] = true;
        cfg.m_Data["auth"]["api_key"] = authKey;
    }

    return cfg;
}

int Config::GetServerPort() const {
    if (m_Data.contains("server") && m_Data["server"].contains("port")) {
        return m_Data["server"]["port"].get<int>();
    }
    return 9001;
}

std::string Config::GetLiteLlmBaseUrl() const {
    if (m_Data.contains("litellm") && m_Data["litellm"].contains("base_url")) {
        return m_Data["litellm"]["base_url"].get<std::string>();
    }
    return "http://localhost:4000";
}

std::string Config::GetDefaultModel() const {
    if (m_Data.contains("litellm") && m_Data["litellm"].contains("default_model")) {
        return m_Data["litellm"]["default_model"].get<std::string>();
    }
    return "gpt-3.5-turbo";
}

size_t Config::GetThreadPoolSize() const {
    if (m_Data.contains("thread_pool") && m_Data["thread_pool"].contains("size")) {
        return m_Data["thread_pool"]["size"].get<size_t>();
    }
    return std::thread::hardware_concurrency();
}

size_t Config::GetMaxRequestBodySize() const {
    if (m_Data.contains("server") && m_Data["server"].contains("max_request_body_bytes")) {
        return m_Data["server"]["max_request_body_bytes"].get<size_t>();
    }
    return 1048576; // 1MB
}

size_t Config::GetRateLimitRequestsPerMinute() const {
    if (m_Data.contains("rate_limit") && m_Data["rate_limit"].contains("requests_per_minute")) {
        return m_Data["rate_limit"]["requests_per_minute"].get<size_t>();
    }
    return 60;
}

bool Config::IsAuthEnabled() const {
    if (m_Data.contains("auth") && m_Data["auth"].contains("enabled")) {
        return m_Data["auth"]["enabled"].get<bool>();
    }
    return false;
}

std::string Config::GetAuthApiKey() const {
    if (m_Data.contains("auth") && m_Data["auth"].contains("api_key")) {
        return m_Data["auth"]["api_key"].get<std::string>();
    }
    return "";
}

std::string Config::GetSkillsDirectory() const {
    if (m_Data.contains("skills") && m_Data["skills"].contains("directory")) {
        return m_Data["skills"]["directory"].get<std::string>();
    }
    return "skills";
}

std::string Config::GetPluginsDirectory() const {
    if (m_Data.contains("plugins") && m_Data["plugins"].contains("directory")) {
        return m_Data["plugins"]["directory"].get<std::string>();
    }
    return "plugins";
}

std::string Config::GetMcpServersConfigPath() const {
    if (m_Data.contains("discovery") && m_Data["discovery"].contains("servers_config")) {
        return m_Data["discovery"]["servers_config"].get<std::string>();
    }
    return "config/mcp_servers.json";
}

const nlohmann::json& Config::GetRaw() const {
    return m_Data;
}
