#pragma once

#include <nlohmann/json.hpp>
#include <string>

/// Unified metadata for MCP tools. Each command can self-describe by
/// overriding ICommandStrategy::metadata() and returning one of these.
struct ToolMetadata {
    std::string name;
    std::string description;
    nlohmann::json inputSchema;       // JSON Schema for MCP tools/list
    std::string defaultModel;          // preferred LLM model (empty = use global default)
    nlohmann::json defaultParameters;  // {temperature, max_tokens, ...} (empty = none)
};
