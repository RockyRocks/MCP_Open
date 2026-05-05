#pragma once
#include <commands/ICommandStrategy.h>
#include <plugins/IPlugin.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>

class NativePluginAdapter
    : public ICommandStrategy
    , public std::enable_shared_from_this<NativePluginAdapter>
{
public:
    static constexpr int kMaxFaults              = 3;
    static constexpr int kDefaultTimeoutSeconds  = 30;
    static constexpr int kMaxConcurrentCalls     = 8;

    NativePluginAdapter(std::shared_ptr<IPlugin> plugin,
                        std::string toolName,
                        std::string description,
                        nlohmann::json inputSchema,
                        int timeoutSeconds = kDefaultTimeoutSeconds);

    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) override;
    ToolMetadata GetMetadata() const override;
    void Cancel(const std::string& requestId) override;
    void Shutdown() override;

    bool IsDisabled() const { return m_FaultCount.load() >= kMaxFaults; }
    int  GetFaultCount() const { return m_FaultCount.load(); }
    int  GetActiveThreads() const { return m_ActiveThreads.load(); }

private:
    std::shared_ptr<IPlugin> m_Plugin;
    std::string              m_ToolName;
    std::string              m_Description;
    nlohmann::json           m_InputSchema;
    int                      m_TimeoutSeconds;
    std::atomic<int>         m_FaultCount{0};
    std::atomic<int>         m_ActiveThreads{0};
    std::atomic<bool>        m_ShutdownRequested{false};

    std::mutex               m_CancelMutex;
    std::set<std::string>    m_CancelledRequests;
};
