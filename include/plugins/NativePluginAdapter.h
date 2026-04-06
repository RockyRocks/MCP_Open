#pragma once
#include <commands/ICommandStrategy.h>
#include <plugins/IPlugin.h>
#include <atomic>
#include <memory>
#include <string>

/// Adapts a single tool from a loaded IPlugin as an ICommandStrategy so it
/// appears as a first-class tool in CommandRegistry and MCP tools/list.
///
/// Fault isolation:
///   - Every call to ExecuteAsync is wrapped in try/catch; exceptions become
///     isError JSON responses rather than crashing the host.
///   - A per-tool fault counter tracks consecutive failures. After kMaxFaults
///     the adapter is disabled: subsequent calls return isError immediately
///     without touching the plugin.
///   - Each call is time-bounded by kDefaultTimeoutSeconds. Calls that exceed
///     the limit receive an isError timeout response (the underlying thread
///     may continue running in-process — full process isolation is future work
///     via a subprocess sandbox).
class NativePluginAdapter : public ICommandStrategy {
public:
    static constexpr int kMaxFaults            = 3;
    static constexpr int kDefaultTimeoutSeconds = 30;

    NativePluginAdapter(std::shared_ptr<IPlugin> plugin,
                        std::string toolName,
                        std::string description,
                        nlohmann::json inputSchema,
                        int timeoutSeconds = kDefaultTimeoutSeconds);

    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) override;
    ToolMetadata GetMetadata() const override;

    bool IsDisabled() const { return m_FaultCount.load() >= kMaxFaults; }
    int  GetFaultCount() const { return m_FaultCount.load(); }

private:
    std::shared_ptr<IPlugin> m_Plugin;
    std::string              m_ToolName;
    std::string              m_Description;
    nlohmann::json           m_InputSchema;
    int                      m_TimeoutSeconds;
    std::atomic<int>         m_FaultCount{0};
};
