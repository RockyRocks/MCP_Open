#include <discovery/CompositeCommand.h>
#include <core/Logger.h>

CompositeCommand::CompositeCommand(std::shared_ptr<McpServerRegistry> registry,
                                     std::shared_ptr<IHttpClient> httpClient)
    : m_Registry(std::move(registry)), m_HttpClient(std::move(httpClient)) {}

std::future<nlohmann::json> CompositeCommand::ExecuteAsync(const nlohmann::json& request) {
    auto reg = m_Registry;
    auto client = m_HttpClient;

    return std::async(std::launch::async, [reg, client, request]() -> nlohmann::json {
        if (!request.contains("payload") || !request["payload"].contains("capability")) {
            return {{"status", "error"}, {"error", "Missing 'capability' in payload"}};
        }

        std::string capability = request["payload"]["capability"].get<std::string>();
        auto server = reg->GetBestServerFor(capability);

        if (!server.has_value()) {
            return {{"status", "error"},
                    {"error", "No server found for capability: " + capability}};
        }

        // Build the forwarded request
        nlohmann::json forwardReq = request["payload"].value("request", nlohmann::json::object());
        if (!forwardReq.contains("command")) {
            forwardReq["command"] = capability;
        }

        std::unordered_map<std::string, std::string> headers = {
            {"Content-Type", "application/json"}
        };

        Logger::GetInstance().Log("Forwarding to " + server->m_Name + " at " + server->m_Url);

        auto resp = client->Post(server->m_Url + "/mcp", forwardReq.dump(),
                                  headers, server->m_TimeoutSeconds);

        if (resp.m_StatusCode != 200) {
            return {{"status", "error"},
                    {"error", "Remote server returned " + std::to_string(resp.m_StatusCode)},
                    {"server", server->m_Name}};
        }

        try {
            auto parsed = nlohmann::json::parse(resp.m_Body);
            parsed["_routed_to"] = server->m_Name;
            return parsed;
        } catch (...) {
            return {{"status", "error"}, {"error", "Invalid response from remote server"}};
        }
    });
}

ToolMetadata CompositeCommand::GetMetadata() const {
    return {
        "remote",
        "Forward request to a remote MCP server by capability",
        {
            {"type", "object"},
            {"properties", {
                {"capability", {{"type", "string"}, {"description", "The capability to route to"}}},
                {"request", {{"type", "object"}, {"description", "The request payload to forward"}}}
            }},
            {"required", nlohmann::json::array({"capability"})}
        },
        "",  // no default model
        {}   // no default parameters
    };
}
