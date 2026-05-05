#pragma once
#include <commands/CommandRegistry.h>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
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

    /// Register a callback invoked when a script plugin is (re)loaded at runtime.
    static void SetNotifyCallback(std::function<void(const nlohmann::json&)> cb);

    /// Start a background thread that monitors plugin.json mtime for changes.
    /// When a plugin.json changes or a new plugin directory appears, re-discovers
    /// tools and registers them.
    static void StartWatcher(const std::string& pluginsDir,
                             std::shared_ptr<CommandRegistry> registry);

    /// Signal the watcher thread to stop and join it.
    static void StopWatcher();

    static constexpr int kWatchIntervalMs = 3000;

private:
    ScriptPluginLoader() = delete;
};
