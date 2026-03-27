#include <security/ApiKeyValidator.h>

ApiKeyValidator::ApiKeyValidator(const std::string& expectedKey, bool enabled)
    : m_ExpectedKey(expectedKey), m_Enabled(enabled) {}

bool ApiKeyValidator::Validate(const std::string& authorizationHeader) const {
    if (!m_Enabled) return true;

    const std::string prefix = "Bearer ";
    if (authorizationHeader.size() <= prefix.size()) return false;
    if (authorizationHeader.substr(0, prefix.size()) != prefix) return false;

    std::string providedKey = authorizationHeader.substr(prefix.size());
    // Constant-time comparison to prevent timing attacks
    if (providedKey.size() != m_ExpectedKey.size()) return false;

    volatile int result = 0;
    for (size_t i = 0; i < providedKey.size(); ++i) {
        result |= providedKey[i] ^ m_ExpectedKey[i];
    }
    return result == 0;
}

bool ApiKeyValidator::IsEnabled() const {
    return m_Enabled;
}
