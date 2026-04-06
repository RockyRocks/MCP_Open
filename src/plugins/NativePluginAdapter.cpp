#include <plugins/NativePluginAdapter.h>
#include <commands/ToolMetadata.h>
#include <core/Logger.h>
#include <future>
#include <chrono>
#include <thread>

NativePluginAdapter::NativePluginAdapter(std::shared_ptr<IPlugin> plugin,
                                         std::string toolName,
                                         std::string description,
                                         nlohmann::json inputSchema,
                                         int timeoutSeconds)
    : m_Plugin(std::move(plugin))
    , m_ToolName(std::move(toolName))
    , m_Description(std::move(description))
    , m_InputSchema(std::move(inputSchema))
    , m_TimeoutSeconds(timeoutSeconds)
{}

std::future<nlohmann::json> NativePluginAdapter::ExecuteAsync(
    const nlohmann::json& request)
{
    return std::async(std::launch::async, [this, request]() -> nlohmann::json {
        if (IsDisabled()) {
            return {
                {"isError", true},
                {"content", {{{"type","text"},
                              {"text","Plugin tool '" + m_ToolName
                                      + "' is disabled after "
                                      + std::to_string(kMaxFaults)
                                      + " consecutive faults"}}}}
            };
        }

        // We need a timeout-capable future that doesn't block its own
        // destructor. Using std::promise + detached std::thread avoids the
        // blocking destructor of std::async(launch::async, ...) futures.
        auto promise = std::make_shared<std::promise<nlohmann::json>>();
        std::future<nlohmann::json> innerFuture = promise->get_future();

        std::thread([this, request, promise]() mutable {
            try {
                promise->set_value(m_Plugin->Execute(m_ToolName, request));
            } catch (const std::exception& ex) {
                try {
                    promise->set_exception(std::make_exception_ptr(ex));
                } catch (...) {}
            } catch (...) {
                try {
                    promise->set_exception(
                        std::make_exception_ptr(
                            std::runtime_error("unknown exception")));
                } catch (...) {}
            }
        }).detach();

        auto status = innerFuture.wait_for(
            std::chrono::seconds(m_TimeoutSeconds));

        if (status == std::future_status::timeout) {
            int faults = ++m_FaultCount;
            Logger::GetInstance().Log(
                "[NativePlugin] timeout executing '" + m_ToolName
                + "' (fault " + std::to_string(faults) + "/"
                + std::to_string(kMaxFaults) + ")");
            return {
                {"isError", true},
                {"content", {{{"type","text"},
                              {"text","Plugin tool '" + m_ToolName
                                      + "' timed out after "
                                      + std::to_string(m_TimeoutSeconds)
                                      + "s"}}}}
            };
        }

        try {
            return innerFuture.get();
        } catch (const std::exception& ex) {
            int faults = ++m_FaultCount;
            Logger::GetInstance().Log(
                "[NativePlugin] exception in '" + m_ToolName
                + "': " + ex.what()
                + " (fault " + std::to_string(faults) + "/"
                + std::to_string(kMaxFaults) + ")");
            return {
                {"isError", true},
                {"content", {{{"type","text"},
                              {"text","Plugin tool '" + m_ToolName
                                      + "' threw: " + ex.what()}}}}
            };
        } catch (...) {
            int faults = ++m_FaultCount;
            Logger::GetInstance().Log(
                "[NativePlugin] unknown exception in '" + m_ToolName
                + "' (fault " + std::to_string(faults) + "/"
                + std::to_string(kMaxFaults) + ")");
            return {
                {"isError", true},
                {"content", {{{"type","text"},
                              {"text","Plugin tool '" + m_ToolName
                                      + "' threw an unknown exception"}}}}
            };
        }
    });
}

ToolMetadata NativePluginAdapter::GetMetadata() const {
    ToolMetadata meta;
    meta.m_Name        = m_ToolName;
    meta.m_Description = m_Description;
    meta.m_InputSchema = m_InputSchema;
    meta.m_Source      = ToolSource::NativePlugin;
    meta.m_Hidden      = false;
    return meta;
}
