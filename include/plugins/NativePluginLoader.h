#pragma once
#include <commands/CommandRegistry.h>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

/// Loads native plugin shared libraries (.dll / .so) into a CommandRegistry
/// and optionally watches the plugins directory for new arrivals at runtime.
///
/// Expected directory layout:
///   plugins/
///     <plugin-name>/
///       bin/
///         <plugin-name>.dll   (Windows)
///         lib<plugin-name>.so (Linux/macOS)
///       plugin.json           (optional manifest — name, description)
///       src/                  (optional source for reference)
///
/// When a plugin loads successfully, a structured notification is emitted via:
///   1. Logger::GetInstance().Log(...) — always
///   2. The registered notify callback (if any) — delivered in-process to
///      connected transports so LLM clients receive a
///      notifications/tools/list_changed event without polling.
class NativePluginLoader {
public:
    /// Register a callback invoked each time a plugin is successfully loaded
    /// (both at startup and at runtime via the watcher).
    /// The JSON payload shape:
    ///   {
    ///     "event":   "plugin_loaded",
    ///     "plugin":  { "name":"...", "description":"...", "version":"..." },
    ///     "tools":   ["tool_a", "tool_b"],
    ///     "source":  "startup" | "runtime"
    ///   }
    static void SetNotifyCallback(std::function<void(const nlohmann::json&)> cb);

    /// Scan pluginsDir, load every recognised shared library, register all
    /// tools into registry. Errors are logged and skipped — one bad plugin
    /// never prevents others from loading.
    static void LoadAll(const std::string& pluginsDir,
                        CommandRegistry& registry);

    /// Load a single shared-library file and register its tools.
    /// Returns true if at least one tool was registered.
    /// `source` is included in the notification payload ("startup" or "runtime").
    static bool LoadOne(const std::string& dlPath,
                        CommandRegistry& registry,
                        const std::string& source = "startup");

    /// Start a background thread that polls pluginsDir every kWatchIntervalMs
    /// milliseconds. When it finds a new .dll / .so that has not been loaded
    /// yet, it calls LoadOne with source="runtime".
    /// Safe to call multiple times — only one watcher thread runs at a time.
    static void StartWatcher(const std::string& pluginsDir,
                             std::shared_ptr<CommandRegistry> registry);

    /// Signal the watcher thread to stop and join it. Call before process exit.
    static void StopWatcher();

    static constexpr int kWatchIntervalMs = 2000;

private:
    NativePluginLoader() = delete;
};
