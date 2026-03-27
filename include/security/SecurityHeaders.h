#pragma once
#include <string>
#include <unordered_map>

class SecurityHeaders {
public:
    static std::unordered_map<std::string, std::string> GetDefaults();
};
