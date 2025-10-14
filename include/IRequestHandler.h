#pragma once
#include <nlohmann/json.hpp>

/// @brief
class IRequestHandler {
public:
    virtual ~IRequestHandler() = default;
    virtual nlohmann::json handle(const nlohmann::json& request) = 0;
};
