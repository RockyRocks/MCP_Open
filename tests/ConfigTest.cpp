#include <gtest/gtest.h>
#include "core/Config.h"
#include <fstream>

TEST(ConfigTest, DefaultValues) {
    Config cfg = Config::loadFromEnv();
    EXPECT_EQ(cfg.serverPort(), 9001);
    EXPECT_EQ(cfg.litellmBaseUrl(), "http://localhost:4000");
    EXPECT_GT(cfg.threadPoolSize(), 0u);
    EXPECT_EQ(cfg.maxRequestBodySize(), 1048576u);
    EXPECT_EQ(cfg.rateLimitRequestsPerMinute(), 60u);
    EXPECT_FALSE(cfg.authEnabled());
}

TEST(ConfigTest, LoadFromFile) {
    std::string path = "test_config_tmp.json";
    {
        std::ofstream f(path);
        f << R"({
            "server": {"port": 8080},
            "litellm": {"base_url": "http://localhost:5000", "default_model": "gpt-4"},
            "rate_limit": {"requests_per_minute": 30}
        })";
    }

    Config cfg = Config::loadFromFile(path);
    EXPECT_EQ(cfg.serverPort(), 8080);
    EXPECT_EQ(cfg.litellmBaseUrl(), "http://localhost:5000");
    EXPECT_EQ(cfg.defaultModel(), "gpt-4");
    EXPECT_EQ(cfg.rateLimitRequestsPerMinute(), 30u);

    std::remove(path.c_str());
}

TEST(ConfigTest, MissingFileThrows) {
    EXPECT_THROW(Config::loadFromFile("nonexistent.json"), std::runtime_error);
}
