#include "security/ApiKeyValidator.h"

ApiKeyValidator::ApiKeyValidator(const std::string& expectedKey, bool enabled)
    : expectedKey_(expectedKey), enabled_(enabled) {}

bool ApiKeyValidator::validate(const std::string& authorizationHeader) const {
    if (!enabled_) return true;

    const std::string prefix = "Bearer ";
    if (authorizationHeader.size() <= prefix.size()) return false;
    if (authorizationHeader.substr(0, prefix.size()) != prefix) return false;

    std::string providedKey = authorizationHeader.substr(prefix.size());
    // Constant-time comparison to prevent timing attacks
    if (providedKey.size() != expectedKey_.size()) return false;

    volatile int result = 0;
    for (size_t i = 0; i < providedKey.size(); ++i) {
        result |= providedKey[i] ^ expectedKey_[i];
    }
    return result == 0;
}

bool ApiKeyValidator::isEnabled() const {
    return enabled_;
}
