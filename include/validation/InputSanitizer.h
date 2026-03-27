#pragma once
#include <nlohmann/json.hpp>
#include <string>

class InputSanitizer {
public:
    static std::string SanitizeString(const std::string& input, size_t maxLength = 10000);
    static bool ValidateJsonDepth(const nlohmann::json& j, int maxDepth = 32);
    static bool ValidatePayloadSize(const std::string& body, size_t maxBytes);

private:
    static bool CheckDepth(const nlohmann::json& j, int currentDepth, int maxDepth);
};
