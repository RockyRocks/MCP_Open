#pragma once
#include <future>
#include <nlohmann/json.hpp>
#include "commands/ToolMetadata.h"

class ICommandStrategy {
public:
    virtual ~ICommandStrategy() = default;
    virtual std::future<nlohmann::json> executeAsync(const nlohmann::json& request) = 0;

    /// Override to provide tool metadata for MCP tools/list.
    /// Default returns empty metadata (name/description filled by registry).
    virtual ToolMetadata metadata() const { return {}; }
};
