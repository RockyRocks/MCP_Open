#pragma once
#include <future>
#include <nlohmann/json.hpp>

class ICommandStrategy {
public:
    virtual ~ICommandStrategy() = default;
    virtual std::future<nlohmann::json> executeAsync(const nlohmann::json& request) = 0;
};
