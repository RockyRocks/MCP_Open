#pragma once
#include <nlohmann/json.hpp>

class IValidator {
public:
    virtual ~IValidator() = default;
    virtual bool validate(const nlohmann::json& request) = 0;
};
