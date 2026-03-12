#include "security/RateLimiter.h"

RateLimiter::RateLimiter(size_t requestsPerMinute)
    : maxTokens_(requestsPerMinute) {}

bool RateLimiter::allowRequest(const std::string& clientIp) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto now = std::chrono::steady_clock::now();

    auto it = buckets_.find(clientIp);
    if (it == buckets_.end()) {
        buckets_[clientIp] = {maxTokens_ - 1, now};
        return true;
    }

    auto& bucket = it->second;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - bucket.lastRefill);

    // Refill tokens proportionally to elapsed time
    if (elapsed.count() > 0) {
        size_t refill = static_cast<size_t>(elapsed.count()) * maxTokens_ / 60;
        bucket.tokens = std::min(maxTokens_, bucket.tokens + refill);
        bucket.lastRefill = now;
    }

    if (bucket.tokens > 0) {
        --bucket.tokens;
        return true;
    }
    return false;
}

void RateLimiter::reset() {
    std::lock_guard<std::mutex> lock(mtx_);
    buckets_.clear();
}
