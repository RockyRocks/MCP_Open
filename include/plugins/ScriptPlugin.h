#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/// Describes a single tool discovered from a script plugin's --mcp-list output.
struct ScriptPluginToolInfo {
    std::string    m_Name;
    std::string    m_Description;
    nlohmann::json m_InputSchema;  // JSON Schema object; {} if absent in plugin output
};

/// Holds everything discovered about one script plugin directory.
/// Populated by ScriptPluginLoader before adapters are registered.
struct ScriptPlugin {
    std::string                       m_Name;        // from plugin.json "name" or dir name
    std::string                       m_Runtime;     // raw value: "python", "node", etc.
    std::string                       m_Entrypoint;  // absolute path to entry script/dll/exe
    std::vector<ScriptPluginToolInfo> m_Tools;       // populated by DiscoverTools()
};
