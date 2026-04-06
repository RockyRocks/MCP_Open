#include <server/StdioTransport.h>
#include <commands/CommandRegistry.h>
#include <skills/SkillEngine.h>
#include <discovery/McpServerRegistry.h>
#include <core/Logger.h>

#include <sstream>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

// JSON-RPC 2.0 error codes
static constexpr int JSONRPC_PARSE_ERROR      = -32700;
static constexpr int JSONRPC_INVALID_REQUEST  = -32600;
static constexpr int JSONRPC_METHOD_NOT_FOUND = -32601;
static constexpr int JSONRPC_INVALID_PARAMS   = -32602;
static constexpr int JSONRPC_INTERNAL_ERROR   = -32603;

static constexpr const char* MCP_PROTOCOL_VERSION = "2024-11-05";

StdioTransport::StdioTransport(
    std::shared_ptr<CommandRegistry> registry,
    std::shared_ptr<SkillEngine> skillEngine,
    std::shared_ptr<McpServerRegistry> mcpRegistry,
    std::istream& input,
    std::ostream& output,
    const std::string& serverName,
    const std::string& serverVersion)
    : m_Registry(std::move(registry))
    , m_SkillEngine(std::move(skillEngine))
    , m_McpRegistry(std::move(mcpRegistry))
    , m_Input(input)
    , m_Output(output)
    , m_ServerName(serverName)
    , m_ServerVersion(serverVersion)
{
}

void StdioTransport::Run() {
#ifdef _WIN32
    // Set binary mode on stdin/stdout to avoid \r\n corruption
    if (&m_Input == &std::cin) {
        _setmode(_fileno(stdin), _O_BINARY);
    }
    if (&m_Output == &std::cout) {
        _setmode(_fileno(stdout), _O_BINARY);
    }
#endif

    // Redirect logger to stderr when using real stdin/stdout
    if (&m_Output == &std::cout) {
        Logger::GetInstance().SetSuppressStdout(true);
        Logger::GetInstance().SetObserver([](const std::string& msg) {
            std::cerr << "[LOG] " << msg << std::endl;
        });
    }

    m_Running = true;
    std::string line;

    while (m_Running && std::getline(m_Input, line)) {
        if (line.empty()) {
            continue;
        }

        // Strip trailing \r if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        nlohmann::json message;
        try {
            message = nlohmann::json::parse(line);
        } catch (const nlohmann::json::parse_error&) {
            SendMessage(MakeError(nullptr, JSONRPC_PARSE_ERROR, "Parse error"));
            continue;
        }

        // Validate basic JSON-RPC structure
        if (!message.is_object() || !message.contains("jsonrpc") ||
            message["jsonrpc"] != "2.0" || !message.contains("method")) {
            auto id = message.contains("id") ? message["id"] : nullptr;
            SendMessage(MakeError(id, JSONRPC_INVALID_REQUEST, "Invalid JSON-RPC 2.0 request"));
            continue;
        }

        // Notifications have no "id" field — process but don't respond
        bool isNotification = !message.contains("id");

        nlohmann::json response = Dispatch(message);

        if (!isNotification && !response.is_null()) {
            SendMessage(response);
        }
    }

    m_Running = false;
}

void StdioTransport::Stop() {
    m_Running = false;
}

void StdioTransport::PushNotification(const nlohmann::json& notification) {
    SendMessage(notification);
}

nlohmann::json StdioTransport::Dispatch(const nlohmann::json& message) {
    std::string method = message["method"].get<std::string>();
    auto id = message.contains("id") ? message["id"] : nullptr;
    auto params = message.value("params", nlohmann::json::object());

    if (method == "initialize") {
        return HandleInitialize(params, id);
    }
    if (method == "notifications/initialized") {
        // Notification — no response
        return nullptr;
    }

    // All methods below require initialization
    if (!m_Initialized) {
        return MakeError(id, JSONRPC_INVALID_REQUEST, "Server not initialized");
    }

    if (method == "tools/list") {
        return HandleToolsList(id);
    }
    if (method == "tools/call") {
        return HandleToolsCall(params, id);
    }
    if (method == "tools/call_batch") {
        return HandleToolsCallBatch(params, id);
    }
    if (method == "prompts/list") {
        return HandlePromptsList(id);
    }
    if (method == "prompts/get") {
        return HandlePromptsGet(params, id);
    }

    return MakeError(id, JSONRPC_METHOD_NOT_FOUND,
                     "Method not found: " + method);
}

// ---------------------------------------------------------------------------
// MCP method handlers
// ---------------------------------------------------------------------------

nlohmann::json StdioTransport::HandleInitialize(
    const nlohmann::json& /*params*/, const nlohmann::json& id) {
    m_Initialized = true;

    nlohmann::json result = {
        {"protocolVersion", MCP_PROTOCOL_VERSION},
        {"capabilities", {
            {"tools", nlohmann::json::object()},
            {"prompts", nlohmann::json::object()}
        }},
        {"serverInfo", {
            {"name", m_ServerName},
            {"version", m_ServerVersion}
        }}
    };

    return MakeResponse(id, result);
}

nlohmann::json StdioTransport::HandleToolsList(const nlohmann::json& id) {
    auto tools = BuildToolList();
    nlohmann::json toolsArray = nlohmann::json::array();

    for (const auto& tool : tools) {
        toolsArray.push_back({
            {"name", tool.m_Name},
            {"description", tool.m_Description},
            {"inputSchema", tool.m_InputSchema}
        });
    }

    return MakeResponse(id, {{"tools", toolsArray}});
}

nlohmann::json StdioTransport::HandleToolsCall(
    const nlohmann::json& params, const nlohmann::json& id) {

    if (!params.contains("name") || !params["name"].is_string()) {
        return MakeError(id, JSONRPC_INVALID_PARAMS,
                         "Missing required parameter: name");
    }

    std::string toolName = params["name"].get<std::string>();
    auto arguments = params.value("arguments", nlohmann::json::object());

    // Check if the tool/command exists
    auto cmd = m_Registry->Resolve(toolName);
    if (!cmd) {
        return MakeError(id, JSONRPC_INVALID_PARAMS,
                         "Unknown tool: " + toolName);
    }

    // Inject per-tool defaults if the caller didn't provide overrides
    auto meta = cmd->GetMetadata();
    if (!meta.m_DefaultModel.empty() && !arguments.contains("model")) {
        arguments["model"] = meta.m_DefaultModel;
    }
    if (!meta.m_DefaultParameters.is_null() && !meta.m_DefaultParameters.empty()
        && !arguments.contains("parameters")) {
        arguments["parameters"] = meta.m_DefaultParameters;
    }

    // Translate MCP tools/call to internal command format
    nlohmann::json internalRequest = {
        {"command", toolName},
        {"payload", arguments}
    };

    try {
        auto future = cmd->ExecuteAsync(internalRequest);
        nlohmann::json result = future.get();

        // Check for command-level errors
        bool isError = result.value("status", "ok") == "error";
        std::string textContent = result.dump();

        nlohmann::json mcpResult = {
            {"content", nlohmann::json::array({
                {{"type", "text"}, {"text", textContent}}
            })},
            {"isError", isError}
        };

        return MakeResponse(id, mcpResult);

    } catch (const std::exception& e) {
        return MakeError(id, JSONRPC_INTERNAL_ERROR, e.what());
    }
}

nlohmann::json StdioTransport::HandleToolsCallBatch(
    const nlohmann::json& params, const nlohmann::json& id) {

    if (!params.contains("calls") || !params["calls"].is_array()) {
        return MakeError(id, JSONRPC_INVALID_PARAMS,
                         "Missing required parameter: calls (array)");
    }

    const auto& calls = params["calls"];
    if (calls.empty()) {
        return MakeResponse(id, {{"results", nlohmann::json::array()}});
    }

    // Phase 1: resolve all commands and launch futures in parallel
    struct PendingCall {
        std::string name;
        std::future<nlohmann::json> future;
        bool isError = false;
        std::string errorMsg;
    };

    std::vector<PendingCall> pending;
    pending.reserve(calls.size());

    for (const auto& call : calls) {
        if (!call.contains("name") || !call["name"].is_string()) {
            PendingCall pc;
            pc.name = "<unknown>";
            pc.isError = true;
            pc.errorMsg = "Each call must have a string 'name' field";
            pending.push_back(std::move(pc));
            continue;
        }

        std::string toolName = call["name"].get<std::string>();
        auto arguments = call.value("arguments", nlohmann::json::object());

        auto cmd = m_Registry->Resolve(toolName);
        if (!cmd) {
            PendingCall pc;
            pc.name = toolName;
            pc.isError = true;
            pc.errorMsg = "Unknown tool: " + toolName;
            pending.push_back(std::move(pc));
            continue;
        }

        auto meta = cmd->GetMetadata();
        if (!meta.m_DefaultModel.empty() && !arguments.contains("model")) {
            arguments["model"] = meta.m_DefaultModel;
        }
        if (!meta.m_DefaultParameters.is_null() && !meta.m_DefaultParameters.empty()
            && !arguments.contains("parameters")) {
            arguments["parameters"] = meta.m_DefaultParameters;
        }

        nlohmann::json internalRequest = {
            {"command", toolName},
            {"payload", arguments}
        };

        PendingCall pc;
        pc.name = toolName;
        try {
            pc.future = cmd->ExecuteAsync(internalRequest);
        } catch (const std::exception& e) {
            pc.isError = true;
            pc.errorMsg = e.what();
        }
        pending.push_back(std::move(pc));
    }

    // Phase 2: collect all results (futures run in parallel during phase 1)
    nlohmann::json results = nlohmann::json::array();
    for (auto& pc : pending) {
        if (pc.isError) {
            results.push_back({
                {"name", pc.name},
                {"isError", true},
                {"content", nlohmann::json::array({
                    {{"type", "text"}, {"text", pc.errorMsg}}
                })}
            });
            continue;
        }

        try {
            nlohmann::json result = pc.future.get();
            bool isError = result.value("status", "ok") == "error";
            results.push_back({
                {"name", pc.name},
                {"isError", isError},
                {"content", nlohmann::json::array({
                    {{"type", "text"}, {"text", result.dump()}}
                })}
            });
        } catch (const std::exception& e) {
            results.push_back({
                {"name", pc.name},
                {"isError", true},
                {"content", nlohmann::json::array({
                    {{"type", "text"}, {"text", std::string(e.what())}}
                })}
            });
        }
    }

    return MakeResponse(id, {{"results", results}});
}

nlohmann::json StdioTransport::HandlePromptsList(const nlohmann::json& id) {
    nlohmann::json promptsArray = nlohmann::json::array();

    if (m_SkillEngine) {
        auto skillsJson = m_SkillEngine->ListSkillsJson();
        for (const auto& skill : skillsJson) {
            nlohmann::json prompt = {
                {"name", skill.value("name", "")},
                {"description", skill.value("description", "")}
            };

            // Map required_variables to MCP prompt arguments
            nlohmann::json args = nlohmann::json::array();
            if (skill.contains("required_variables")) {
                for (const auto& var : skill["required_variables"]) {
                    args.push_back({
                        {"name", var.get<std::string>()},
                        {"description", "Required variable"},
                        {"required", true}
                    });
                }
            }
            prompt["arguments"] = args;
            promptsArray.push_back(prompt);
        }
    }

    return MakeResponse(id, {{"prompts", promptsArray}});
}

nlohmann::json StdioTransport::HandlePromptsGet(
    const nlohmann::json& params, const nlohmann::json& id) {

    if (!params.contains("name") || !params["name"].is_string()) {
        return MakeError(id, JSONRPC_INVALID_PARAMS,
                         "Missing required parameter: name");
    }

    std::string promptName = params["name"].get<std::string>();
    auto arguments = params.value("arguments", nlohmann::json::object());

    if (!m_SkillEngine) {
        return MakeError(id, JSONRPC_INVALID_PARAMS,
                         "No skill engine available");
    }

    auto skill = m_SkillEngine->Resolve(promptName);
    if (!skill) {
        return MakeError(id, JSONRPC_INVALID_PARAMS,
                         "Unknown prompt: " + promptName);
    }

    try {
        std::string rendered = m_SkillEngine->RenderPrompt(*skill, arguments);

        nlohmann::json result = {
            {"messages", nlohmann::json::array({
                {
                    {"role", "user"},
                    {"content", {
                        {"type", "text"},
                        {"text", rendered}
                    }}
                }
            })}
        };

        return MakeResponse(id, result);

    } catch (const std::exception& e) {
        return MakeError(id, JSONRPC_INVALID_PARAMS, e.what());
    }
}

// ---------------------------------------------------------------------------
// JSON-RPC helpers
// ---------------------------------------------------------------------------

nlohmann::json StdioTransport::MakeResponse(
    const nlohmann::json& id, const nlohmann::json& result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

nlohmann::json StdioTransport::MakeError(
    const nlohmann::json& id, int code,
    const std::string& message, const nlohmann::json& data) {
    nlohmann::json error = {
        {"code", code},
        {"message", message}
    };
    if (!data.is_null()) {
        error["data"] = data;
    }
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", error}
    };
}

void StdioTransport::SendMessage(const nlohmann::json& msg) {
    std::lock_guard<std::mutex> lock(m_WriteMutex);
    m_Output << msg.dump() << "\n";
    m_Output.flush();
}

// ---------------------------------------------------------------------------
// Tool metadata
// ---------------------------------------------------------------------------

std::vector<StdioTransport::ToolMeta> StdioTransport::BuildToolList() const {
    std::vector<ToolMeta> tools;
    for (auto& meta : m_Registry->ListToolMetadata()) {
        tools.push_back({
            std::move(meta.m_Name),
            std::move(meta.m_Description),
            std::move(meta.m_InputSchema)
        });
    }
    return tools;
}
