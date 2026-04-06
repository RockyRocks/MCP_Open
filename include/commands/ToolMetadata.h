#pragma once

#include <nlohmann/json.hpp>
#include <string>

/// Where a tool came from. Used by REST endpoints to filter by category.
enum class ToolSource {
    BuiltIn,      ///< Compiled-in command (echo, llm, remote)
    JsonSkill,    ///< Promoted from a skills/*.json file
    Plugin,       ///< Promoted from a plugins/*/skills/*/SKILL.md file
    NativePlugin, ///< Loaded from a native shared library (.dll / .so)
};

/// Unified metadata for MCP tools. Each command can self-describe by
/// overriding ICommandStrategy::GetMetadata() and returning one of these.
struct ToolMetadata {
    std::string m_Name;
    std::string m_Description;
    nlohmann::json m_InputSchema;       // JSON Schema for MCP tools/list
    std::string m_DefaultModel;         // preferred LLM model (empty = use global default)
    nlohmann::json m_DefaultParameters; // {temperature, max_tokens, ...} (empty = none)
    ToolSource m_Source = ToolSource::BuiltIn; ///< origin of this tool
    bool m_Hidden = false;              ///< if true, omit from MCP tools/list
};
