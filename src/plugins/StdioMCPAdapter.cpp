#include <plugins/StdioMCPAdapter.h>
#include <commands/ToolMetadata.h>
#include <core/Logger.h>

#include <future>
#include <stdexcept>

StdioMCPAdapter::StdioMCPAdapter(std::string pluginName,
                                  std::string command,
                                  std::vector<std::string> args,
                                  std::string toolName,
                                  std::string description,
                                  nlohmann::json inputSchema)
    : m_PluginName(std::move(pluginName))
    , m_Command(std::move(command))
    , m_Args(std::move(args))
    , m_ToolName(std::move(toolName))
    , m_Description(std::move(description))
    , m_InputSchema(std::move(inputSchema))
{}

ToolMetadata StdioMCPAdapter::GetMetadata() const {
    ToolMetadata meta;
    meta.m_Name        = m_ToolName;
    meta.m_Description = m_Description;
    meta.m_InputSchema = m_InputSchema.is_null() || m_InputSchema.empty()
        ? nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}}
        : m_InputSchema;
    meta.m_Source = ToolSource::ScriptPlugin;
    meta.m_Hidden = false;
    return meta;
}

bool StdioMCPAdapter::EnsureRunning() {
    if (m_Pipe && m_Pipe->IsRunning() && m_Initialized)
        return true;

    if (m_RespawnCount >= kMaxRespawnRetries) {
        Logger::GetInstance().Log(
            "[StdioMCP] " + m_PluginName + ": max respawn retries reached");
        return false;
    }

    try {
        m_Pipe = SubprocessPipe::Spawn(m_Command, m_Args);
        ++m_RespawnCount;

        nlohmann::json initReq = {
            {"jsonrpc", "2.0"},
            {"id", m_NextId.fetch_add(1)},
            {"method", "initialize"},
            {"params", {
                {"protocolVersion", "2024-11-05"},
                {"capabilities", nlohmann::json::object()},
                {"clientInfo", {{"name", "mcp-server-cmake"}, {"version", "1.0.0"}}}
            }}
        };

        if (!m_Pipe->WriteLine(initReq.dump())) {
            Logger::GetInstance().Log(
                "[StdioMCP] " + m_PluginName + ": failed to send initialize");
            return false;
        }

        std::string response;
        if (!m_Pipe->ReadLine(response, kDefaultTimeoutMs)) {
            Logger::GetInstance().Log(
                "[StdioMCP] " + m_PluginName + ": initialize timeout");
            return false;
        }

        auto initResp = nlohmann::json::parse(response);
        if (initResp.contains("error")) {
            Logger::GetInstance().Log(
                "[StdioMCP] " + m_PluginName + ": initialize error: "
                + initResp["error"].dump());
            return false;
        }

        // Send initialized notification
        nlohmann::json notification = {
            {"jsonrpc", "2.0"},
            {"method", "notifications/initialized"}
        };
        m_Pipe->WriteLine(notification.dump());

        m_Initialized = true;
        Logger::GetInstance().Log(
            "[StdioMCP] " + m_PluginName + ": initialized (respawn #"
            + std::to_string(m_RespawnCount) + ")");
        return true;

    } catch (const std::exception& e) {
        Logger::GetInstance().Log(
            "[StdioMCP] " + m_PluginName + ": spawn failed: " + e.what());
        return false;
    }
}

nlohmann::json StdioMCPAdapter::SendRequest(const std::string& method,
                                              const nlohmann::json& params) {
    std::lock_guard<std::mutex> lock(m_PipeMutex);

    if (!EnsureRunning()) {
        return {
            {"isError", true},
            {"content", {{{"type", "text"},
                          {"text", "MCP server '" + m_PluginName + "' is not running"}}}}
        };
    }

    int id = m_NextId.fetch_add(1);
    nlohmann::json req = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", params}
    };

    if (!m_Pipe->WriteLine(req.dump())) {
        m_Initialized = false;
        return {
            {"isError", true},
            {"content", {{{"type", "text"},
                          {"text", "Failed to send request to '" + m_PluginName + "'"}}}}
        };
    }

    std::string response;
    if (!m_Pipe->ReadLine(response, kDefaultTimeoutMs)) {
        m_Initialized = false;
        return {
            {"isError", true},
            {"content", {{{"type", "text"},
                          {"text", "Timeout waiting for response from '" + m_PluginName + "'"}}}}
        };
    }

    try {
        auto result = nlohmann::json::parse(response);

        if (result.contains("error")) {
            std::string errMsg = result["error"].value("message", "Unknown error");
            return {
                {"isError", true},
                {"content", {{{"type", "text"}, {"text", errMsg}}}}
            };
        }

        if (result.contains("result"))
            return result["result"];

        return result;

    } catch (const nlohmann::json::parse_error& e) {
        return {
            {"isError", true},
            {"content", {{{"type", "text"},
                          {"text", std::string("Invalid JSON from MCP server: ") + e.what()}}}}
        };
    }
}

std::future<nlohmann::json> StdioMCPAdapter::ExecuteAsync(
    const nlohmann::json& request)
{
    auto toolName   = m_ToolName;
    auto pluginName = m_PluginName;

    return std::async(std::launch::async,
        [this, toolName, request]() -> nlohmann::json {
            nlohmann::json args = request.value("payload",
                                  request.value("arguments", nlohmann::json::object()));

            nlohmann::json params = {
                {"name", toolName},
                {"arguments", args}
            };

            return SendRequest("tools/call", params);
        });
}

// Static discovery: spawn a temporary process, initialize, query tools/list, shut down
std::vector<ScriptPluginToolInfo> StdioMCPAdapter::DiscoverTools(
    const std::string& pluginName,
    const std::string& command,
    const std::vector<std::string>& args)
{
    std::vector<ScriptPluginToolInfo> result;

    std::unique_ptr<SubprocessPipe> pipe;
    try {
        pipe = SubprocessPipe::Spawn(command, args);
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(
            "[StdioMCP] " + pluginName + ": spawn failed during discovery: " + e.what());
        return {};
    }

    // Send initialize
    nlohmann::json initReq = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", nlohmann::json::object()},
            {"clientInfo", {{"name", "mcp-server-cmake"}, {"version", "1.0.0"}}}
        }}
    };

    if (!pipe->WriteLine(initReq.dump())) {
        Logger::GetInstance().Log(
            "[StdioMCP] " + pluginName + ": failed to send initialize during discovery");
        return {};
    }

    std::string response;
    if (!pipe->ReadLine(response, kDefaultTimeoutMs)) {
        Logger::GetInstance().Log(
            "[StdioMCP] " + pluginName + ": initialize timeout during discovery");
        return {};
    }

    try {
        auto initResp = nlohmann::json::parse(response);
        if (initResp.contains("error")) {
            Logger::GetInstance().Log(
                "[StdioMCP] " + pluginName + ": initialize error: "
                + initResp["error"].dump());
            return {};
        }
    } catch (...) {
        Logger::GetInstance().Log(
            "[StdioMCP] " + pluginName + ": invalid initialize response");
        return {};
    }

    // Send initialized notification
    nlohmann::json notification = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"}
    };
    pipe->WriteLine(notification.dump());

    // Send tools/list
    nlohmann::json listReq = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/list"},
        {"params", nlohmann::json::object()}
    };

    if (!pipe->WriteLine(listReq.dump())) {
        Logger::GetInstance().Log(
            "[StdioMCP] " + pluginName + ": failed to send tools/list");
        return {};
    }

    if (!pipe->ReadLine(response, kDefaultTimeoutMs)) {
        Logger::GetInstance().Log(
            "[StdioMCP] " + pluginName + ": tools/list timeout");
        return {};
    }

    try {
        auto listResp = nlohmann::json::parse(response);
        if (listResp.contains("error")) {
            Logger::GetInstance().Log(
                "[StdioMCP] " + pluginName + ": tools/list error: "
                + listResp["error"].dump());
            return {};
        }

        auto tools = listResp.value("result", nlohmann::json::object())
                             .value("tools", nlohmann::json::array());

        for (const auto& tool : tools) {
            if (!tool.contains("name") || !tool["name"].is_string()) continue;

            ScriptPluginToolInfo info;
            info.m_Name        = tool["name"].get<std::string>();
            info.m_Description = tool.value("description", "");
            if (tool.contains("inputSchema") && tool["inputSchema"].is_object())
                info.m_InputSchema = tool["inputSchema"];
            result.push_back(std::move(info));
        }

    } catch (const std::exception& e) {
        Logger::GetInstance().Log(
            "[StdioMCP] " + pluginName + ": tools/list parse failed: " + e.what());
        return {};
    }

    Logger::GetInstance().Log(
        "[StdioMCP] " + pluginName + ": discovered "
        + std::to_string(result.size()) + " tools");

    return result;
}
