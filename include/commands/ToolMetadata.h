#pragma once

#include <nlohmann/json.hpp>
#include <string>

/// Unified metadata for MCP tools. Each command can self-describe by
/// overriding ICommandStrategy::GetMetadata() and returning one of these.
struct ToolMetadata {
    std::string m_Name;
    std::string m_Description;
    nlohmann::json m_InputSchema;       // JSON Schema for MCP tools/list
    std::string m_DefaultModel;          // preferred LLM model (empty = use global default)
    nlohmann::json m_DefaultParameters;  // {temperature, max_tokens, ...} (empty = none)
};
