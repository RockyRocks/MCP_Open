#pragma once
#include <commands/ICommandStrategy.h>
#include <plugins/SubprocessPipe.h>
#include <plugins/ScriptPlugin.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class StdioMCPAdapter
    : public ICommandStrategy
    , public std::enable_shared_from_this<StdioMCPAdapter>
{
public:
    static constexpr int kDefaultTimeoutMs  = 30000;
    static constexpr int kMaxRespawnRetries = 3;

    StdioMCPAdapter(std::string pluginName,
                    std::string command,
                    std::vector<std::string> args,
                    std::string toolName,
                    std::string description,
                    nlohmann::json inputSchema);

    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) override;
    ToolMetadata                GetMetadata() const override;
    void Cancel(const std::string& requestId) override;
    void Shutdown() override;

    static std::vector<ScriptPluginToolInfo> DiscoverTools(
        const std::string& pluginName,
        const std::string& command,
        const std::vector<std::string>& args);

private:
    nlohmann::json SendRequest(const std::string& method,
                               const nlohmann::json& params);
    bool EnsureRunning();

    std::string              m_PluginName;
    std::string              m_Command;
    std::vector<std::string> m_Args;
    std::string              m_ToolName;
    std::string              m_Description;
    nlohmann::json           m_InputSchema;

    std::unique_ptr<SubprocessPipe> m_Pipe;
    std::mutex                      m_PipeMutex;
    std::atomic<int>                m_NextId{1};
    int                             m_RespawnCount{0};
    bool                            m_Initialized{false};
    std::atomic<bool>               m_ShutdownRequested{false};

    std::mutex                      m_CancelMutex;
    std::map<std::string, int>      m_InFlightRequests;
};
