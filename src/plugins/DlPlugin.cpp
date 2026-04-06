#include <plugins/DlPlugin.h>
#include <core/Logger.h>
#include <stdexcept>
#include <cstring>

#ifndef _WIN32
#include <dlfcn.h>
#endif

// ---------------------------------------------------------------------------
// Platform helpers
// ---------------------------------------------------------------------------

static LibHandle PlatformOpen(const std::string& path) {
#ifdef _WIN32
    return LoadLibraryA(path.c_str());
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

static void* PlatformSym(LibHandle lib, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(lib, name));
#else
    return dlsym(lib, name);
#endif
}

static void PlatformClose(LibHandle lib) {
    if (!lib) return;
#ifdef _WIN32
    FreeLibrary(lib);
#else
    dlclose(lib);
#endif
}

static std::string PlatformError() {
#ifdef _WIN32
    DWORD err = GetLastError();
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, sizeof(buf), nullptr);
    return std::string(buf);
#else
    const char* e = dlerror();
    return e ? std::string(e) : "unknown dl error";
#endif
}

// ---------------------------------------------------------------------------
// DlPlugin
// ---------------------------------------------------------------------------

DlPlugin::~DlPlugin() {
    if (m_PluginHandle && m_FnDestroy) {
        try {
            m_FnDestroy(m_PluginHandle);
        } catch (...) {}
    }
    PlatformClose(m_LibHandle);
}

template<typename T>
static T ResolveSymbol(LibHandle lib, const char* name) {
    return reinterpret_cast<T>(PlatformSym(lib, name));
}

std::unique_ptr<DlPlugin> DlPlugin::Load(const std::string& path) {
    LibHandle lib = PlatformOpen(path);
    if (!lib) {
        Logger::GetInstance().Log("[DlPlugin] Failed to open '" + path
                                  + "': " + PlatformError());
        return nullptr;
    }

    // Resolve all required symbols up-front
    auto fnApiVersion = ResolveSymbol<mcp_api_version_fn>(lib, MCP_SYM_API_VERSION);
    auto fnManifest   = ResolveSymbol<mcp_manifest_fn>   (lib, MCP_SYM_MANIFEST);
    auto fnCreate     = ResolveSymbol<mcp_create_fn>     (lib, MCP_SYM_CREATE);
    auto fnDestroy    = ResolveSymbol<mcp_destroy_fn>    (lib, MCP_SYM_DESTROY);
    auto fnListTools  = ResolveSymbol<mcp_list_tools_fn> (lib, MCP_SYM_LIST_TOOLS);
    auto fnExecute    = ResolveSymbol<mcp_execute_fn>    (lib, MCP_SYM_EXECUTE);
    auto fnFreeString = ResolveSymbol<mcp_free_string_fn>(lib, MCP_SYM_FREE_STRING);

    const char* missing = nullptr;
    if (!fnApiVersion) missing = MCP_SYM_API_VERSION;
    else if (!fnManifest)   missing = MCP_SYM_MANIFEST;
    else if (!fnCreate)     missing = MCP_SYM_CREATE;
    else if (!fnDestroy)    missing = MCP_SYM_DESTROY;
    else if (!fnListTools)  missing = MCP_SYM_LIST_TOOLS;
    else if (!fnExecute)    missing = MCP_SYM_EXECUTE;
    else if (!fnFreeString) missing = MCP_SYM_FREE_STRING;

    if (missing) {
        Logger::GetInstance().Log("[DlPlugin] '" + path
                                  + "' missing required export: " + missing);
        PlatformClose(lib);
        return nullptr;
    }

    // Version check
    uint32_t pluginVer = fnApiVersion();
    uint32_t hostVer   = MCP_PLUGIN_API_VERSION;
    uint16_t pluginMajor = static_cast<uint16_t>(pluginVer >> 16);
    uint16_t hostMajor   = static_cast<uint16_t>(hostVer   >> 16);
    uint16_t pluginMinor = static_cast<uint16_t>(pluginVer & 0xFFFF);
    uint16_t hostMinor   = static_cast<uint16_t>(hostVer   & 0xFFFF);

    if (pluginMajor != hostMajor) {
        Logger::GetInstance().Log("[DlPlugin] '" + path + "' refused: major version "
                                  + std::to_string(pluginMajor)
                                  + " != host major " + std::to_string(hostMajor));
        PlatformClose(lib);
        return nullptr;
    }
    if (pluginMinor > hostMinor) {
        Logger::GetInstance().Log("[DlPlugin] '" + path + "' warning: plugin minor "
                                  + std::to_string(pluginMinor)
                                  + " > host minor " + std::to_string(hostMinor)
                                  + " — some features may be unavailable");
    }

    // Parse manifest
    const char* manifestRaw = fnManifest();
    std::string name, description, version;
    try {
        auto manifest = nlohmann::json::parse(manifestRaw ? manifestRaw : "{}");
        name        = manifest.value("name",        "");
        description = manifest.value("description", "");
        version     = manifest.value("version",     "0.0.0");
    } catch (const std::exception& e) {
        Logger::GetInstance().Log("[DlPlugin] '" + path
                                  + "' bad manifest JSON: " + e.what());
        PlatformClose(lib);
        return nullptr;
    }

    if (name.empty()) {
        Logger::GetInstance().Log("[DlPlugin] '" + path
                                  + "' manifest missing 'name' field");
        PlatformClose(lib);
        return nullptr;
    }

    // Create plugin instance
    void* handle = fnCreate();
    if (!handle) {
        Logger::GetInstance().Log("[DlPlugin] '" + path
                                  + "' mcp_plugin_create() returned NULL");
        PlatformClose(lib);
        return nullptr;
    }

    auto plugin = std::unique_ptr<DlPlugin>(new DlPlugin());
    plugin->m_LibHandle    = lib;
    plugin->m_PluginHandle = handle;
    plugin->m_FnApiVersion = fnApiVersion;
    plugin->m_FnDestroy    = fnDestroy;
    plugin->m_FnListTools  = fnListTools;
    plugin->m_FnExecute    = fnExecute;
    plugin->m_FnFreeString = fnFreeString;
    plugin->m_Name         = std::move(name);
    plugin->m_Description  = std::move(description);
    plugin->m_Version      = std::move(version);
    plugin->m_Path         = path;

    return plugin;
}

std::vector<PluginToolInfo> DlPlugin::ListTools() const {
    const char* raw = m_FnListTools(m_PluginHandle);
    if (!raw) return {};

    std::vector<PluginToolInfo> tools;
    try {
        auto arr = nlohmann::json::parse(raw);
        if (!arr.is_array()) return {};
        tools.reserve(arr.size());
        for (const auto& item : arr) {
            PluginToolInfo t;
            t.m_Name        = item.value("name",        "");
            t.m_Description = item.value("description", "");
            t.m_InputSchema = item.value("inputSchema",
                                        nlohmann::json::object());
            if (!t.m_Name.empty()) {
                tools.push_back(std::move(t));
            }
        }
    } catch (const std::exception& e) {
        Logger::GetInstance().Log("[DlPlugin] '" + m_Path
                                  + "' bad tool list JSON: " + e.what());
    }
    return tools;
}

nlohmann::json DlPlugin::Execute(const std::string& toolName,
                                 const nlohmann::json& request) const {
    std::string reqStr = request.dump();
    char* raw = m_FnExecute(m_PluginHandle, toolName.c_str(), reqStr.c_str());
    if (!raw) {
        return {{"isError", true},
                {"content", {{{"type","text"},
                              {"text","Plugin returned null response"}}}}};
    }

    nlohmann::json result;
    try {
        result = nlohmann::json::parse(raw);
    } catch (const std::exception& e) {
        std::string err = e.what();
        m_FnFreeString(raw);
        return {{"isError", true},
                {"content", {{{"type","text"},
                              {"text","Plugin returned malformed JSON: " + err}}}}};
    }
    m_FnFreeString(raw);
    return result;
}
