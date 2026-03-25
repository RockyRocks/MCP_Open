#pragma once

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

class CommandRegistry;
class SkillEngine;
class McpServerRegistry;

/// Implements MCP protocol (JSON-RPC 2.0) over stdio transport.
/// Reads newline-delimited JSON-RPC messages from an input stream,
/// dispatches to existing CommandRegistry/SkillEngine, and writes
/// responses to an output stream.
class StdioTransport {
public:
    StdioTransport(
        std::shared_ptr<CommandRegistry> registry,
        std::shared_ptr<SkillEngine> skillEngine,
        std::shared_ptr<McpServerRegistry> mcpRegistry,
        std::istream& input = std::cin,
        std::ostream& output = std::cout,
        const std::string& serverName = "mcp-open",
        const std::string& serverVersion = "1.0.0"
    );

    /// Blocking read loop. Returns when input stream reaches EOF or stop() is called.
    void run();

    /// Signal the transport to stop after the current message.
    void stop();

private:
    // JSON-RPC dispatch
    nlohmann::json dispatch(const nlohmann::json& message);

    // MCP method handlers
    nlohmann::json handleInitialize(const nlohmann::json& params, const nlohmann::json& id);
    nlohmann::json handleToolsList(const nlohmann::json& id);
    nlohmann::json handleToolsCall(const nlohmann::json& params, const nlohmann::json& id);
    nlohmann::json handlePromptsList(const nlohmann::json& id);
    nlohmann::json handlePromptsGet(const nlohmann::json& params, const nlohmann::json& id);

    // JSON-RPC helpers
    nlohmann::json makeResponse(const nlohmann::json& id, const nlohmann::json& result);
    nlohmann::json makeError(const nlohmann::json& id, int code,
                             const std::string& message,
                             const nlohmann::json& data = nullptr);
    void sendMessage(const nlohmann::json& msg);

    // Tool metadata
    struct ToolMeta {
        std::string name;
        std::string description;
        nlohmann::json inputSchema;
    };
    std::vector<ToolMeta> buildToolList() const;

    std::shared_ptr<CommandRegistry> registry_;
    std::shared_ptr<SkillEngine> skillEngine_;
    std::shared_ptr<McpServerRegistry> mcpRegistry_;
    std::istream& input_;
    std::ostream& output_;
    std::string serverName_;
    std::string serverVersion_;
    bool initialized_ = false;
    std::atomic<bool> running_{false};
    std::mutex writeMutex_;
};
