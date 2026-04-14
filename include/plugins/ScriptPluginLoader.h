#pragma once
#include <commands/CommandRegistry.h>
#include <string>

/// Scans a plugins directory for script plugins (those with a "runtime" key
/// in plugin.json) and registers each discovered tool as a ScriptPluginAdapter.
///
/// Expected layout:
///   plugins/
///     <name>/
///       plugin.json    (must contain "runtime" and "entrypoint" keys)
///       scripts/
///         plugin.py    (or .js / .dll / .exe — relative to plugin dir)
///
/// Native plugin directories (those with bin/*.dll) may also have plugin.json
/// without a "runtime" key — ScriptPluginLoader silently skips them.
///
/// Errors (missing file, bad JSON, tool discovery failure) are logged and
/// skipped gracefully — one broken plugin never prevents others from loading.
class ScriptPluginLoader {
public:
    /// Scan pluginsDir, find all script plugins, discover their tools, and
    /// register each tool as a ScriptPluginAdapter in registry.
    static void LoadAll(const std::string& pluginsDir, CommandRegistry& registry);

private:
    ScriptPluginLoader() = delete;
};
