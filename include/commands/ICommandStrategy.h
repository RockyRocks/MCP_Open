#pragma once
#include <future>
#include <nlohmann/json.hpp>
#include <commands/ToolMetadata.h>

class ICommandStrategy {
public:
    virtual ~ICommandStrategy() = default;
    virtual std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) = 0;

    /// Override to provide tool metadata for MCP tools/list.
    virtual ToolMetadata GetMetadata() const = 0;
};
