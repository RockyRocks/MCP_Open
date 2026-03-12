#pragma once
#include <string>

class ApiKeyValidator {
public:
    explicit ApiKeyValidator(const std::string& expectedKey, bool enabled = true);
    bool validate(const std::string& authorizationHeader) const;
    bool isEnabled() const;

private:
    std::string expectedKey_;
    bool enabled_;
};
