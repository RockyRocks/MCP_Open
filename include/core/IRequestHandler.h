#pragma once
#include <nlohmann/json.hpp>

class IRequestHandler {
public:
    virtual ~IRequestHandler() = default;
    virtual nlohmann::json Handle(const nlohmann::json& request) = 0;
};
