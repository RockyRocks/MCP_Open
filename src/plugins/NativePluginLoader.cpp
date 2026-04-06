#include <plugins/NativePluginLoader.h>
#include <plugins/DlPlugin.h>
#include <plugins/NativePluginAdapter.h>
#include <core/Logger.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_set>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Module-level state for the watcher thread
// ---------------------------------------------------------------------------

namespace {

std::function<void(const nlohmann::json&)> g_NotifyCallback;
std::mutex                                 g_NotifyMutex;

std::atomic<bool>  g_WatcherStop{false};
std::thread        g_WatcherThread;
std::mutex         g_WatcherMutex;   // guards g_WatcherThread state

void FireNotification(const nlohmann::json& payload) {
    Logger::GetInstance().Log("[NativePlugin] plugin loaded: "
                              + payload.dump());
    std::lock_guard<std::mutex> lock(g_NotifyMutex);
    if (g_NotifyCallback) {
        try {
            g_NotifyCallback(payload);
        } catch (...) {}
    }
}

// Returns true if the filename looks like a loadable plugin binary.
bool IsPluginBinary(const fs::path& p) {
    auto ext = p.extension().string();
#ifdef _WIN32
    return ext == ".dll";
#elif defined(__APPLE__)
    return ext == ".dylib" || ext == ".so";
#else
    return ext == ".so";
#endif
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// NativePluginLoader
// ---------------------------------------------------------------------------

void NativePluginLoader::SetNotifyCallback(
    std::function<void(const nlohmann::json&)> cb)
{
    std::lock_guard<std::mutex> lock(g_NotifyMutex);
    g_NotifyCallback = std::move(cb);
}

bool NativePluginLoader::LoadOne(const std::string& dlPath,
                                 CommandRegistry& registry,
                                 const std::string& source)
{
    auto plugin = DlPlugin::Load(dlPath);
    if (!plugin) {
        return false;  // DlPlugin::Load already logged the reason
    }

    auto tools = plugin->ListTools();
    if (tools.empty()) {
        Logger::GetInstance().Log("[NativePlugin] '" + dlPath
                                  + "' loaded but exposes no tools — skipping");
        return false;
    }

    // Wrap plugin in shared_ptr so all adapters share ownership
    auto sharedPlugin = std::shared_ptr<IPlugin>(std::move(plugin));

    nlohmann::json toolNames = nlohmann::json::array();
    int registered = 0;
    for (const auto& toolInfo : tools) {
        if (toolInfo.m_Name.empty()) continue;

        auto adapter = std::make_shared<NativePluginAdapter>(
            sharedPlugin,
            toolInfo.m_Name,
            toolInfo.m_Description,
            toolInfo.m_InputSchema);

        registry.RegisterCommand(toolInfo.m_Name, adapter);
        toolNames.push_back(toolInfo.m_Name);
        ++registered;
    }

    if (registered == 0) {
        return false;
    }

    // Build and fire the plugin-loaded notification
    nlohmann::json notification = {
        {"event",  "plugin_loaded"},
        {"plugin", {
            {"name",        sharedPlugin->GetName()},
            {"description", sharedPlugin->GetDescription()},
            {"version",     sharedPlugin->GetVersion()}
        }},
        {"tools",  toolNames},
        {"source", source}
    };
    FireNotification(notification);
    return true;
}

void NativePluginLoader::LoadAll(const std::string& pluginsDir,
                                 CommandRegistry& registry)
{
    fs::path root(pluginsDir);
    if (!fs::exists(root) || !fs::is_directory(root)) {
        Logger::GetInstance().Log("[NativePlugin] plugins directory '"
                                  + pluginsDir + "' not found — skipping");
        return;
    }

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory()) continue;

        fs::path binDir = entry.path() / "bin";
        if (!fs::exists(binDir) || !fs::is_directory(binDir)) continue;

        for (const auto& binEntry : fs::directory_iterator(binDir, ec)) {
            if (!binEntry.is_regular_file()) continue;
            if (!IsPluginBinary(binEntry.path())) continue;

            LoadOne(binEntry.path().string(), registry, "startup");
        }
    }
}

void NativePluginLoader::StartWatcher(const std::string& pluginsDir,
                                      std::shared_ptr<CommandRegistry> registry)
{
    std::lock_guard<std::mutex> lock(g_WatcherMutex);
    if (g_WatcherThread.joinable()) {
        return;  // already running
    }

    g_WatcherStop = false;

    g_WatcherThread = std::thread([pluginsDir, registry]() {
        // Remember which paths we have already loaded
        std::unordered_set<std::string> loaded;

        // Seed the set with whatever is already present at watcher start
        fs::path root(pluginsDir);
        std::error_code ec;
        if (fs::exists(root, ec) && fs::is_directory(root, ec)) {
            for (const auto& entry : fs::directory_iterator(root, ec)) {
                if (!entry.is_directory()) continue;
                fs::path binDir = entry.path() / "bin";
                if (!fs::exists(binDir, ec)) continue;
                for (const auto& binEntry : fs::directory_iterator(binDir, ec)) {
                    if (binEntry.is_regular_file()
                        && IsPluginBinary(binEntry.path()))
                    {
                        loaded.insert(binEntry.path().string());
                    }
                }
            }
        }

        while (!g_WatcherStop.load()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(NativePluginLoader::kWatchIntervalMs));

            if (g_WatcherStop.load()) break;
            if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) continue;

            for (const auto& entry : fs::directory_iterator(root, ec)) {
                if (!entry.is_directory()) continue;
                fs::path binDir = entry.path() / "bin";
                if (!fs::exists(binDir, ec)) continue;

                for (const auto& binEntry : fs::directory_iterator(binDir, ec)) {
                    if (!binEntry.is_regular_file()) continue;
                    if (!IsPluginBinary(binEntry.path())) continue;

                    std::string path = binEntry.path().string();
                    if (loaded.count(path)) continue;  // already loaded

                    Logger::GetInstance().Log(
                        "[NativePlugin] watcher detected new plugin: " + path);

                    if (NativePluginLoader::LoadOne(path, *registry, "runtime")) {
                        loaded.insert(path);
                    }
                }
            }
        }

        Logger::GetInstance().Log("[NativePlugin] watcher stopped");
    });
}

void NativePluginLoader::StopWatcher() {
    g_WatcherStop = true;
    std::lock_guard<std::mutex> lock(g_WatcherMutex);
    if (g_WatcherThread.joinable()) {
        g_WatcherThread.join();
    }
}
