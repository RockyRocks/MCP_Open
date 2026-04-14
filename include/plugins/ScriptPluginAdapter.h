#pragma once
#include <commands/ICommandStrategy.h>
#include <plugins/ScriptPlugin.h>
#include <string>
#include <vector>

/// Adapts a single script plugin tool as an ICommandStrategy.
///
/// Discovery (load-time, static):
///   ScriptPluginAdapter::DiscoverTools() runs the entrypoint with --mcp-list,
///   parses the JSON array, validates each tool name, and returns a vector of
///   ScriptPluginToolInfo. Called once per plugin directory at startup.
///
/// Execution (per-call, instance):
///   ExecuteAsync() writes the request arguments to a temp JSON file
///   (temp_directory_path() / "mcp_script_<counter>_<pid>.json"),
///   invokes the entrypoint with --mcp-call <toolName> --mcp-args-file <path>,
///   reads stdout, removes the temp file (always, via RAII guard), and
///   parses the result JSON.
///
///   Per-call subprocess spawn — no persistent process management.
///
/// Supported runtimes in plugin.json:
///   "python"     → "python" (Windows) / "python3" (Linux/Mac)
///   "node"       → "node"
///   "dotnet"     → "dotnet"
///   "executable" → entrypoint is the executable, no runtime prefix
///   <any other>  → used as-is (allows "python3", "node20", etc.)
class ScriptPluginAdapter : public ICommandStrategy {
public:
    static constexpr size_t kMaxOutputBytes = 4 * 1024 * 1024;  // 4 MiB guard

    ScriptPluginAdapter(std::string pluginName,
                        std::string runtime,
                        std::string entrypoint,
                        ScriptPluginToolInfo toolInfo);

    // ICommandStrategy
    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) override;
    ToolMetadata                GetMetadata() const override;

    /// Called by ScriptPluginLoader at load time — runs --mcp-list once per plugin.
    static std::vector<ScriptPluginToolInfo> DiscoverTools(
        const std::string& pluginName,
        const std::string& runtime,
        const std::string& entrypoint);

    /// Map runtime string to the actual executable name.
    /// Exposed static so unit tests can verify platform-specific mapping.
    static std::string GetRuntimeExecutable(const std::string& runtime);

    /// Build the --mcp-list discovery command string.
    static std::string BuildListCommand(const std::string& runtime,
                                        const std::string& entrypoint);

    /// Build the --mcp-call execution command string.
    /// toolName must have already been validated by IsValidToolName().
    static std::string BuildCallCommand(const std::string& runtime,
                                        const std::string& entrypoint,
                                        const std::string& toolName,
                                        const std::string& argsFilePath);

    /// Returns true iff name matches [a-zA-Z0-9_-]+ (non-empty).
    static bool IsValidToolName(const std::string& name);

private:
    std::string          m_PluginName;
    std::string          m_Runtime;
    std::string          m_Entrypoint;
    ScriptPluginToolInfo m_ToolInfo;
};
