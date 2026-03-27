#include <validation/InputSanitizer.h>
#include <algorithm>

std::string InputSanitizer::SanitizeString(const std::string& input, size_t maxLength) {
    std::string result;
    result.reserve(std::min(input.size(), maxLength));

    for (size_t i = 0; i < input.size() && result.size() < maxLength; ++i) {
        char c = input[i];
        // Allow printable ASCII, newlines, tabs
        if (c >= 32 || c == '\n' || c == '\r' || c == '\t') {
            result += c;
        }
    }
    return result;
}

bool InputSanitizer::ValidateJsonDepth(const nlohmann::json& j, int maxDepth) {
    return CheckDepth(j, 0, maxDepth);
}

bool InputSanitizer::ValidatePayloadSize(const std::string& body, size_t maxBytes) {
    return body.size() <= maxBytes;
}

bool InputSanitizer::CheckDepth(const nlohmann::json& j, int currentDepth, int maxDepth) {
    if (currentDepth > maxDepth) return false;

    if (j.is_object()) {
        for (const auto& [key, val] : j.items()) {
            if (!CheckDepth(val, currentDepth + 1, maxDepth)) return false;
        }
    } else if (j.is_array()) {
        for (const auto& elem : j) {
            if (!CheckDepth(elem, currentDepth + 1, maxDepth)) return false;
        }
    }
    return true;
}
