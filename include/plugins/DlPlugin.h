#pragma once
#include <plugins/IPlugin.h>
#include <plugins/PluginABI.h>
#include <memory>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
using LibHandle = HMODULE;
#else
using LibHandle = void*;
#endif

/// Concrete IPlugin implementation that loads a native shared library via
/// dlopen (Unix) or LoadLibraryA (Windows), resolves the MCP plugin ABI
/// function pointers, version-checks, and calls mcp_plugin_create.
///
/// Destruction calls mcp_plugin_destroy then FreeLibrary/dlclose so the
/// DL is kept alive for exactly the lifetime of this object.
class DlPlugin final : public IPlugin {
public:
    ~DlPlugin() override;

    DlPlugin(const DlPlugin&) = delete;
    DlPlugin& operator=(const DlPlugin&) = delete;

    /// Attempt to load the shared library at `path`.
    /// Returns nullptr (with a Logger warning) if:
    ///   - the file can't be opened
    ///   - a required export is missing
    ///   - the major API version differs from MCP_PLUGIN_API_VERSION
    ///   - mcp_plugin_create() returns NULL
    static std::unique_ptr<DlPlugin> Load(const std::string& path);

    const std::string& GetName()        const override { return m_Name; }
    const std::string& GetDescription() const override { return m_Description; }
    const std::string& GetVersion()     const override { return m_Version; }
    const std::string& GetPath()        const override { return m_Path; }

    std::vector<PluginToolInfo> ListTools() const override;
    nlohmann::json Execute(const std::string& toolName,
                           const nlohmann::json& request) const override;

private:
    DlPlugin() = default;

    LibHandle          m_LibHandle    = nullptr;
    void*              m_PluginHandle = nullptr;

    mcp_api_version_fn m_FnApiVersion = nullptr;
    mcp_destroy_fn     m_FnDestroy    = nullptr;
    mcp_list_tools_fn  m_FnListTools  = nullptr;
    mcp_execute_fn     m_FnExecute    = nullptr;
    mcp_free_string_fn m_FnFreeString = nullptr;

    std::string m_Name;
    std::string m_Description;
    std::string m_Version;
    std::string m_Path;
};
