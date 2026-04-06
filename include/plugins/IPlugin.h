#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/// Metadata for a single tool exposed by a native plugin.
struct PluginToolInfo {
    std::string m_Name;
    std::string m_Description;
    nlohmann::json m_InputSchema;
};

/// Abstract interface for a loaded native plugin.
/// Concrete implementation: DlPlugin (dlopen/LoadLibrary).
/// Test doubles implement this interface directly without DL machinery.
class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual const std::string& GetName()        const = 0;
    virtual const std::string& GetDescription() const = 0;
    virtual const std::string& GetVersion()     const = 0;
    virtual const std::string& GetPath()        const = 0;

    /// List all tools the plugin exposes.
    virtual std::vector<PluginToolInfo> ListTools() const = 0;

    /// Execute a tool by name. The request object contains the tool's
    /// input arguments as defined by the tool's inputSchema.
    /// Returns a JSON result (MCP content format) or an isError response.
    /// Must NOT throw across the plugin boundary.
    virtual nlohmann::json Execute(const std::string& toolName,
                                   const nlohmann::json& request) const = 0;
};
