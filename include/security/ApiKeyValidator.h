#pragma once
#include <string>

class ApiKeyValidator {
public:
    explicit ApiKeyValidator(const std::string& expectedKey, bool enabled = true);
    bool Validate(const std::string& authorizationHeader) const;
    bool IsEnabled() const;

private:
    std::string m_ExpectedKey;
    bool m_Enabled;
};
