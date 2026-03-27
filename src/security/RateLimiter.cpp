#include <security/RateLimiter.h>

RateLimiter::RateLimiter(size_t requestsPerMinute)
    : m_MaxTokens(requestsPerMinute) {}

bool RateLimiter::AllowRequest(const std::string& clientIp) {
    std::lock_guard<std::mutex> lock(m_Mtx);
    auto now = std::chrono::steady_clock::now();

    auto it = m_Buckets.find(clientIp);
    if (it == m_Buckets.end()) {
        m_Buckets[clientIp] = {m_MaxTokens - 1, now};
        return true;
    }

    auto& bucket = it->second;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - bucket.m_LastRefill);

    // Refill tokens proportionally to elapsed time
    if (elapsed.count() > 0) {
        size_t refill = static_cast<size_t>(elapsed.count()) * m_MaxTokens / 60;
        bucket.m_Tokens = std::min(m_MaxTokens, bucket.m_Tokens + refill);
        bucket.m_LastRefill = now;
    }

    if (bucket.m_Tokens > 0) {
        --bucket.m_Tokens;
        return true;
    }
    return false;
}

void RateLimiter::Reset() {
    std::lock_guard<std::mutex> lock(m_Mtx);
    m_Buckets.clear();
}
