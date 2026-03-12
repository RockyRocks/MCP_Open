#include "security/SecurityHeaders.h"

std::unordered_map<std::string, std::string> SecurityHeaders::getDefaults() {
    return {
        {"X-Content-Type-Options", "nosniff"},
        {"X-Frame-Options", "DENY"},
        {"X-XSS-Protection", "1; mode=block"},
        {"Content-Security-Policy", "default-src 'none'"},
        {"Cache-Control", "no-store"},
        {"Strict-Transport-Security", "max-age=31536000; includeSubDomains"}
    };
}
