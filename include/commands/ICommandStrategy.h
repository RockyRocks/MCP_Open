#pragma once
#include <future>
#include <string>
#include <nlohmann/json.hpp>
#include <commands/ToolMetadata.h>

class ICommandStrategy {
public:
    virtual ~ICommandStrategy() = default;
    virtual std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) = 0;
    virtual ToolMetadata GetMetadata() const = 0;

    virtual void Cancel(const std::string& /*requestId*/) {}
    virtual void Shutdown() {}
};
