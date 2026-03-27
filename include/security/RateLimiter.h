#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

class RateLimiter {
public:
    explicit RateLimiter(size_t requestsPerMinute = 60);
    bool AllowRequest(const std::string& clientIp);
    void Reset();

private:
    struct ClientBucket {
        size_t m_Tokens;
        std::chrono::steady_clock::time_point m_LastRefill;
    };

    size_t m_MaxTokens;
    std::unordered_map<std::string, ClientBucket> m_Buckets;
    std::mutex m_Mtx;
};
