#include "server/StdioTransport.h"
#include "commands/CommandRegistry.h"
#include "skills/SkillEngine.h"
#include "discovery/McpServerRegistry.h"
#include "core/Logger.h"

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
    : registry_(std::move(registry))
    , skillEngine_(std::move(skillEngine))
    , mcpRegistry_(std::move(mcpRegistry))
    , input_(input)
    , output_(output)
    , serverName_(serverName)
    , serverVersion_(serverVersion)
{
}

void StdioTransport::run() {
#ifdef _WIN32
    // Set binary mode on stdin/stdout to avoid \r\n corruption
    if (&input_ == &std::cin) {
        _setmode(_fileno(stdin), _O_BINARY);
    }
    if (&output_ == &std::cout) {
        _setmode(_fileno(stdout), _O_BINARY);
    }
#endif

    // Redirect logger to stderr when using real stdin/stdout
    if (&output_ == &std::cout) {
        Logger::getInstance().setSuppressStdout(true);
        Logger::getInstance().setObserver([](const std::string& msg) {
            std::cerr << "[LOG] " << msg << std::endl;
        });
    }

    running_ = true;
    std::string line;

    while (running_ && std::getline(input_, line)) {
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
            sendMessage(makeError(nullptr, JSONRPC_PARSE_ERROR, "Parse error"));
            continue;
        }

        // Validate basic JSON-RPC structure
        if (!message.is_object() || !message.contains("jsonrpc") ||
            message["jsonrpc"] != "2.0" || !message.contains("method")) {
            auto id = message.contains("id") ? message["id"] : nullptr;
            sendMessage(makeError(id, JSONRPC_INVALID_REQUEST, "Invalid JSON-RPC 2.0 request"));
            continue;
        }

        // Notifications have no "id" field — process but don't respond
        bool isNotification = !message.contains("id");

        nlohmann::json response = dispatch(message);

        if (!isNotification && !response.is_null()) {
            sendMessage(response);
        }
    }

    running_ = false;
}

void StdioTransport::stop() {
    running_ = false;
}

nlohmann::json StdioTransport::dispatch(const nlohmann::json& message) {
    std::string method = message["method"].get<std::string>();
    auto id = message.contains("id") ? message["id"] : nullptr;
    auto params = message.value("params", nlohmann::json::object());

    if (method == "initialize") {
        return handleInitialize(params, id);
    }
    if (method == "notifications/initialized") {
        // Notification — no response
        return nullptr;
    }

    // All methods below require initialization
    if (!initialized_) {
        return makeError(id, JSONRPC_INVALID_REQUEST, "Server not initialized");
    }

    if (method == "tools/list") {
        return handleToolsList(id);
    }
    if (method == "tools/call") {
        return handleToolsCall(params, id);
    }
    if (method == "prompts/list") {
        return handlePromptsList(id);
    }
    if (method == "prompts/get") {
        return handlePromptsGet(params, id);
    }

    return makeError(id, JSONRPC_METHOD_NOT_FOUND,
                     "Method not found: " + method);
}

// ---------------------------------------------------------------------------
// MCP method handlers
// ---------------------------------------------------------------------------

nlohmann::json StdioTransport::handleInitialize(
    const nlohmann::json& /*params*/, const nlohmann::json& id) {
    initialized_ = true;

    nlohmann::json result = {
        {"protocolVersion", MCP_PROTOCOL_VERSION},
        {"capabilities", {
            {"tools", nlohmann::json::object()},
            {"prompts", nlohmann::json::object()}
        }},
        {"serverInfo", {
            {"name", serverName_},
            {"version", serverVersion_}
        }}
    };

    return makeResponse(id, result);
}

nlohmann::json StdioTransport::handleToolsList(const nlohmann::json& id) {
    auto tools = buildToolList();
    nlohmann::json toolsArray = nlohmann::json::array();

    for (const auto& tool : tools) {
        toolsArray.push_back({
            {"name", tool.name},
            {"description", tool.description},
            {"inputSchema", tool.inputSchema}
        });
    }

    return makeResponse(id, {{"tools", toolsArray}});
}

nlohmann::json StdioTransport::handleToolsCall(
    const nlohmann::json& params, const nlohmann::json& id) {

    if (!params.contains("name") || !params["name"].is_string()) {
        return makeError(id, JSONRPC_INVALID_PARAMS,
                         "Missing required parameter: name");
    }

    std::string toolName = params["name"].get<std::string>();
    auto arguments = params.value("arguments", nlohmann::json::object());

    // Check if the tool/command exists
    auto cmd = registry_->resolve(toolName);
    if (!cmd) {
        return makeError(id, JSONRPC_INVALID_PARAMS,
                         "Unknown tool: " + toolName);
    }

    // Inject per-tool defaults if the caller didn't provide overrides
    auto meta = cmd->metadata();
    if (!meta.defaultModel.empty() && !arguments.contains("model")) {
        arguments["model"] = meta.defaultModel;
    }
    if (!meta.defaultParameters.is_null() && !meta.defaultParameters.empty()
        && !arguments.contains("parameters")) {
        arguments["parameters"] = meta.defaultParameters;
    }

    // Translate MCP tools/call to internal command format
    nlohmann::json internalRequest = {
        {"command", toolName},
        {"payload", arguments}
    };

    try {
        auto future = cmd->executeAsync(internalRequest);
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

        return makeResponse(id, mcpResult);

    } catch (const std::exception& e) {
        return makeError(id, JSONRPC_INTERNAL_ERROR, e.what());
    }
}

nlohmann::json StdioTransport::handlePromptsList(const nlohmann::json& id) {
    nlohmann::json promptsArray = nlohmann::json::array();

    if (skillEngine_) {
        auto skillsJson = skillEngine_->listSkillsJson();
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

    return makeResponse(id, {{"prompts", promptsArray}});
}

nlohmann::json StdioTransport::handlePromptsGet(
    const nlohmann::json& params, const nlohmann::json& id) {

    if (!params.contains("name") || !params["name"].is_string()) {
        return makeError(id, JSONRPC_INVALID_PARAMS,
                         "Missing required parameter: name");
    }

    std::string promptName = params["name"].get<std::string>();
    auto arguments = params.value("arguments", nlohmann::json::object());

    if (!skillEngine_) {
        return makeError(id, JSONRPC_INVALID_PARAMS,
                         "No skill engine available");
    }

    auto skill = skillEngine_->resolve(promptName);
    if (!skill) {
        return makeError(id, JSONRPC_INVALID_PARAMS,
                         "Unknown prompt: " + promptName);
    }

    try {
        std::string rendered = skillEngine_->renderPrompt(*skill, arguments);

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

        return makeResponse(id, result);

    } catch (const std::exception& e) {
        return makeError(id, JSONRPC_INVALID_PARAMS, e.what());
    }
}

// ---------------------------------------------------------------------------
// JSON-RPC helpers
// ---------------------------------------------------------------------------

nlohmann::json StdioTransport::makeResponse(
    const nlohmann::json& id, const nlohmann::json& result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

nlohmann::json StdioTransport::makeError(
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

void StdioTransport::sendMessage(const nlohmann::json& msg) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    output_ << msg.dump() << "\n";
    output_.flush();
}

// ---------------------------------------------------------------------------
// Tool metadata
// ---------------------------------------------------------------------------

std::vector<StdioTransport::ToolMeta> StdioTransport::buildToolList() const {
    std::vector<ToolMeta> tools;
    for (auto& meta : registry_->listToolMetadata()) {
        tools.push_back({
            std::move(meta.name),
            std::move(meta.description),
            std::move(meta.inputSchema)
        });
    }
    return tools;
}
