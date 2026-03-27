#pragma once
#include <nlohmann/json.hpp>

class IValidator {
public:
    virtual ~IValidator() = default;
    virtual bool Validate(const nlohmann::json& request) = 0;
};
