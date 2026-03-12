#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

class RateLimiter {
public:
    explicit RateLimiter(size_t requestsPerMinute = 60);
    bool allowRequest(const std::string& clientIp);
    void reset();

private:
    struct ClientBucket {
        size_t tokens;
        std::chrono::steady_clock::time_point lastRefill;
    };

    size_t maxTokens_;
    std::unordered_map<std::string, ClientBucket> buckets_;
    std::mutex mtx_;
};
