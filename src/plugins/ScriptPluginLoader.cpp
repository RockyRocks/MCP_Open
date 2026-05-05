#include <plugins/ScriptPluginLoader.h>
#include <plugins/ScriptPluginAdapter.h>
#include <plugins/StdioMCPAdapter.h>
#include <core/Logger.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {
std::function<void(const nlohmann::json&)> g_ScriptNotifyCallback;
std::mutex                                 g_ScriptNotifyMutex;
std::atomic<bool>                          g_ScriptWatcherStop{false};
std::thread                                g_ScriptWatcherThread;
std::mutex                                 g_ScriptWatcherMutex;

void FireScriptNotification(const nlohmann::json& payload) {
    Logger::GetInstance().Log("[ScriptPlugin] plugin reloaded: " + payload.dump());
    std::lock_guard<std::mutex> lock(g_ScriptNotifyMutex);
    if (g_ScriptNotifyCallback) {
        try { g_ScriptNotifyCallback(payload); } catch (...) {}
    }
}
}  // anonymous namespace

void ScriptPluginLoader::SetNotifyCallback(
    std::function<void(const nlohmann::json&)> cb) {
    std::lock_guard<std::mutex> lock(g_ScriptNotifyMutex);
    g_ScriptNotifyCallback = std::move(cb);
}

void ScriptPluginLoader::StartWatcher(const std::string& pluginsDir,
                                       std::shared_ptr<CommandRegistry> registry) {
    std::lock_guard<std::mutex> lock(g_ScriptWatcherMutex);
    if (g_ScriptWatcherThread.joinable()) return;

    g_ScriptWatcherStop = false;

    g_ScriptWatcherThread = std::thread([pluginsDir, registry]() {
        // Track plugin.json mtime for each plugin directory
        std::unordered_map<std::string, fs::file_time_type> mtimeMap;

        fs::path root(pluginsDir);
        std::error_code ec;
        if (fs::exists(root, ec) && fs::is_directory(root, ec)) {
            for (const auto& entry : fs::directory_iterator(root, ec)) {
                if (!entry.is_directory()) continue;
                fs::path jsonPath = entry.path() / "plugin.json";
                if (!fs::exists(jsonPath, ec)) continue;
                auto lwt = fs::last_write_time(jsonPath, ec);
                if (!ec) mtimeMap[jsonPath.string()] = lwt;
            }
        }

        while (!g_ScriptWatcherStop.load()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(ScriptPluginLoader::kWatchIntervalMs));

            if (g_ScriptWatcherStop.load()) break;
            if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) continue;

            for (const auto& entry : fs::directory_iterator(root, ec)) {
                if (!entry.is_directory()) continue;
                fs::path jsonPath = entry.path() / "plugin.json";
                if (!fs::exists(jsonPath, ec)) continue;

                auto lwt = fs::last_write_time(jsonPath, ec);
                if (ec) continue;

                auto it = mtimeMap.find(jsonPath.string());
                if (it != mtimeMap.end() && it->second == lwt) continue;

                mtimeMap[jsonPath.string()] = lwt;

                // New or changed plugin — reload it
                try {
                    std::ifstream f(jsonPath);
                    if (!f.is_open()) continue;
                    std::ostringstream ss;
                    ss << f.rdbuf();
                    auto pluginJson = nlohmann::json::parse(ss.str());
                    if (!pluginJson.contains("runtime")) continue;

                    std::string runtime    = pluginJson["runtime"].get<std::string>();
                    std::string entrypoint = pluginJson.value("entrypoint", "");
                    std::string name       = pluginJson.value("name",
                                               entry.path().filename().string());
                    if (runtime.empty() || entrypoint.empty()) continue;

                    fs::path absEntrypoint = (entry.path() / entrypoint).lexically_normal();
                    if (!fs::exists(absEntrypoint)) continue;

                    Logger::GetInstance().Log(
                        "[ScriptPlugin] watcher: reloading " + name);

                    nlohmann::json toolNames = nlohmann::json::array();
                    int reloaded = 0;

                    if (runtime == "mcp-stdio") {
                        std::string cmd = ScriptPluginAdapter::GetRuntimeExecutable(
                            pluginJson.value("command_runtime", "python"));
                        std::vector<std::string> spawnArgs;
                        spawnArgs.push_back(absEntrypoint.string());
                        if (pluginJson.contains("args") && pluginJson["args"].is_array()) {
                            for (const auto& a : pluginJson["args"])
                                if (a.is_string()) spawnArgs.push_back(a.get<std::string>());
                        }
                        auto tools = StdioMCPAdapter::DiscoverTools(name, cmd, spawnArgs);
                        for (const auto& tool : tools) {
                            registry->RegisterCommand(
                                tool.m_Name,
                                std::make_shared<StdioMCPAdapter>(
                                    name, cmd, spawnArgs,
                                    tool.m_Name, tool.m_Description, tool.m_InputSchema));
                            toolNames.push_back(tool.m_Name);
                            ++reloaded;
                        }
                    } else {
                        auto tools = ScriptPluginAdapter::DiscoverTools(
                            name, runtime, absEntrypoint.string());
                        for (const auto& tool : tools) {
                            registry->RegisterCommand(
                                tool.m_Name,
                                std::make_shared<ScriptPluginAdapter>(
                                    name, runtime, absEntrypoint.string(), tool));
                            toolNames.push_back(tool.m_Name);
                            ++reloaded;
                        }
                    }

                    if (reloaded > 0) {
                        FireScriptNotification({
                            {"event",  "script_plugin_reloaded"},
                            {"plugin", name},
                            {"tools",  toolNames}
                        });
                    }
                } catch (const std::exception& e) {
                    Logger::GetInstance().Log(
                        "[ScriptPlugin] watcher error: " + std::string(e.what()));
                }
            }
        }

        Logger::GetInstance().Log("[ScriptPlugin] watcher stopped");
    });
}

void ScriptPluginLoader::StopWatcher() {
    g_ScriptWatcherStop = true;
    std::lock_guard<std::mutex> lock(g_ScriptWatcherMutex);
    if (g_ScriptWatcherThread.joinable()) {
        g_ScriptWatcherThread.join();
    }
}

void ScriptPluginLoader::LoadAll(const std::string& pluginsDir,
                                  CommandRegistry& registry)
{
    if (!fs::exists(pluginsDir) || !fs::is_directory(pluginsDir)) {
        Logger::GetInstance().Log(
            "[ScriptPlugin] Plugins directory not found: " + pluginsDir + " (skipping)");
        return;
    }

    std::error_code rootEc;
    fs::path canonRoot = fs::canonical(fs::path(pluginsDir), rootEc);
    if (rootEc) {
        Logger::GetInstance().Log(
            "[ScriptPlugin] Cannot resolve plugins root: " + rootEc.message());
        return;
    }

    int loaded = 0;
    for (const auto& entry : fs::directory_iterator(pluginsDir)) {
        if (!entry.is_directory()) continue;

        fs::path pluginDir  = entry.path();
        fs::path jsonPath   = pluginDir / "plugin.json";

        if (!fs::exists(jsonPath)) continue;

        try {
            // Read plugin.json
            std::ifstream f(jsonPath);
            if (!f.is_open()) {
                Logger::GetInstance().Log(
                    "[ScriptPlugin] Cannot open " + jsonPath.string() + " (skipping)");
                continue;
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            auto pluginJson = nlohmann::json::parse(ss.str());

            // No "runtime" key → native plugin or SKILL.md plugin → skip silently
            if (!pluginJson.contains("runtime")) continue;

            std::string runtime    = pluginJson["runtime"].get<std::string>();
            std::string entrypoint = pluginJson.value("entrypoint", "");
            std::string name       = pluginJson.value("name",
                                       pluginDir.filename().string());

            if (runtime.empty() || entrypoint.empty()) {
                Logger::GetInstance().Log(
                    "[ScriptPlugin] Missing runtime or entrypoint in "
                    + jsonPath.string() + " (skipping)");
                continue;
            }

            // Resolve entrypoint to absolute path (relative to plugin directory)
            fs::path absEntrypoint = (pluginDir / entrypoint).lexically_normal();
            if (!fs::exists(absEntrypoint)) {
                Logger::GetInstance().Log(
                    "[ScriptPlugin] Entrypoint not found: " + absEntrypoint.string()
                    + " (skipping)");
                continue;
            }

            // Path traversal protection: ensure entrypoint resolves within plugins root
            std::error_code epEc;
            fs::path canonEntrypoint = fs::canonical(absEntrypoint, epEc);
            if (epEc || canonEntrypoint.string().rfind(canonRoot.string(), 0) != 0) {
                Logger::GetInstance().Log(
                    "[ScriptPlugin] Path traversal blocked: " + absEntrypoint.string()
                    + " (skipping)");
                continue;
            }

            if (runtime == "mcp-stdio") {
                // Persistent MCP child process (FastMCP, mcp-python-sdk, etc.)
                std::string cmd = ScriptPluginAdapter::GetRuntimeExecutable(
                    pluginJson.value("command_runtime", "python"));
                std::vector<std::string> spawnArgs;
                spawnArgs.push_back(absEntrypoint.string());
                if (pluginJson.contains("args") && pluginJson["args"].is_array()) {
                    for (const auto& a : pluginJson["args"])
                        if (a.is_string()) spawnArgs.push_back(a.get<std::string>());
                }

                auto tools = StdioMCPAdapter::DiscoverTools(name, cmd, spawnArgs);
                if (tools.empty()) {
                    Logger::GetInstance().Log(
                        "[StdioMCP] No tools discovered from " + name + " (skipping)");
                    continue;
                }

                for (const auto& tool : tools) {
                    registry.RegisterCommand(
                        tool.m_Name,
                        std::make_shared<StdioMCPAdapter>(
                            name, cmd, spawnArgs,
                            tool.m_Name, tool.m_Description, tool.m_InputSchema));
                    Logger::GetInstance().Log(
                        "[StdioMCP] Registered tool '" + tool.m_Name
                        + "' from plugin '" + name + "'");
                    ++loaded;
                }
                continue;
            }

            // Standard per-call script plugins (--mcp-list / --mcp-call protocol)
            auto tools = ScriptPluginAdapter::DiscoverTools(
                name, runtime, absEntrypoint.string());

            if (tools.empty()) {
                Logger::GetInstance().Log(
                    "[ScriptPlugin] No tools discovered from " + name + " (skipping)");
                continue;
            }

            for (const auto& tool : tools) {
                registry.RegisterCommand(
                    tool.m_Name,
                    std::make_shared<ScriptPluginAdapter>(
                        name, runtime, absEntrypoint.string(), tool));
                Logger::GetInstance().Log(
                    "[ScriptPlugin] Registered tool '" + tool.m_Name
                    + "' from plugin '" + name + "'");
                ++loaded;
            }

        } catch (const std::exception& e) {
            Logger::GetInstance().Log(
                "[ScriptPlugin] Error loading from " + pluginDir.string()
                + ": " + e.what());
        }
    }

    Logger::GetInstance().Log(
        "[ScriptPlugin] Loaded " + std::to_string(loaded) + " tool(s)");
}
