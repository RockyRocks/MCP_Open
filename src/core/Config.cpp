#include "core/Config.h"
#include <fstream>
#include <stdexcept>
#include <thread>

Config Config::loadFromFile(const std::string& path) {
    Config cfg;
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + path);
    }
    cfg.data_ = nlohmann::json::parse(file);
    return cfg;
}

Config Config::loadFromEnv() {
    Config cfg;
    cfg.data_ = nlohmann::json::object();

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
    if (!port.empty()) cfg.data_["server"]["port"] = std::stoi(port);

    std::string litellmUrl = getEnv("MCP_LITELLM_URL");
    if (!litellmUrl.empty()) cfg.data_["litellm"]["base_url"] = litellmUrl;

    std::string model = getEnv("MCP_DEFAULT_MODEL");
    if (!model.empty()) cfg.data_["litellm"]["default_model"] = model;

    std::string authKey = getEnv("MCP_AUTH_API_KEY");
    if (!authKey.empty()) {
        cfg.data_["auth"]["enabled"] = true;
        cfg.data_["auth"]["api_key"] = authKey;
    }

    return cfg;
}

int Config::serverPort() const {
    if (data_.contains("server") && data_["server"].contains("port")) {
        return data_["server"]["port"].get<int>();
    }
    return 9001;
}

std::string Config::litellmBaseUrl() const {
    if (data_.contains("litellm") && data_["litellm"].contains("base_url")) {
        return data_["litellm"]["base_url"].get<std::string>();
    }
    return "http://localhost:4000";
}

std::string Config::defaultModel() const {
    if (data_.contains("litellm") && data_["litellm"].contains("default_model")) {
        return data_["litellm"]["default_model"].get<std::string>();
    }
    return "gpt-3.5-turbo";
}

size_t Config::threadPoolSize() const {
    if (data_.contains("thread_pool") && data_["thread_pool"].contains("size")) {
        return data_["thread_pool"]["size"].get<size_t>();
    }
    return std::thread::hardware_concurrency();
}

size_t Config::maxRequestBodySize() const {
    if (data_.contains("server") && data_["server"].contains("max_request_body_bytes")) {
        return data_["server"]["max_request_body_bytes"].get<size_t>();
    }
    return 1048576; // 1MB
}

size_t Config::rateLimitRequestsPerMinute() const {
    if (data_.contains("rate_limit") && data_["rate_limit"].contains("requests_per_minute")) {
        return data_["rate_limit"]["requests_per_minute"].get<size_t>();
    }
    return 60;
}

bool Config::authEnabled() const {
    if (data_.contains("auth") && data_["auth"].contains("enabled")) {
        return data_["auth"]["enabled"].get<bool>();
    }
    return false;
}

std::string Config::authApiKey() const {
    if (data_.contains("auth") && data_["auth"].contains("api_key")) {
        return data_["auth"]["api_key"].get<std::string>();
    }
    return "";
}

std::string Config::skillsDirectory() const {
    if (data_.contains("skills") && data_["skills"].contains("directory")) {
        return data_["skills"]["directory"].get<std::string>();
    }
    return "skills";
}

std::string Config::mcpServersConfigPath() const {
    if (data_.contains("discovery") && data_["discovery"].contains("servers_config")) {
        return data_["discovery"]["servers_config"].get<std::string>();
    }
    return "config/mcp_servers.json";
}

const nlohmann::json& Config::raw() const {
    return data_;
}
