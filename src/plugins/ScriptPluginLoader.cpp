#include <plugins/ScriptPluginLoader.h>
#include <plugins/ScriptPluginAdapter.h>
#include <core/Logger.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

void ScriptPluginLoader::LoadAll(const std::string& pluginsDir,
                                  CommandRegistry& registry)
{
    if (!fs::exists(pluginsDir) || !fs::is_directory(pluginsDir)) {
        Logger::GetInstance().Log(
            "[ScriptPlugin] Plugins directory not found: " + pluginsDir + " (skipping)");
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

            // Discover tools via --mcp-list
            auto tools = ScriptPluginAdapter::DiscoverTools(
                name, runtime, absEntrypoint.string());

            if (tools.empty()) {
                Logger::GetInstance().Log(
                    "[ScriptPlugin] No tools discovered from " + name + " (skipping)");
                continue;
            }

            // Register each tool
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
