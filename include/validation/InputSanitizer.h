#pragma once
#include <nlohmann/json.hpp>
#include <string>

class InputSanitizer {
public:
    static std::string sanitizeString(const std::string& input, size_t maxLength = 10000);
    static bool validateJsonDepth(const nlohmann::json& j, int maxDepth = 32);
    static bool validatePayloadSize(const std::string& body, size_t maxBytes);

private:
    static bool checkDepth(const nlohmann::json& j, int currentDepth, int maxDepth);
};
