#include "discovery/CompositeCommand.h"
#include "core/Logger.h"

CompositeCommand::CompositeCommand(std::shared_ptr<McpServerRegistry> registry,
                                     std::shared_ptr<IHttpClient> httpClient)
    : registry_(std::move(registry)), httpClient_(std::move(httpClient)) {}

std::future<nlohmann::json> CompositeCommand::executeAsync(const nlohmann::json& request) {
    auto reg = registry_;
    auto client = httpClient_;

    return std::async(std::launch::async, [reg, client, request]() -> nlohmann::json {
        if (!request.contains("payload") || !request["payload"].contains("capability")) {
            return {{"status", "error"}, {"error", "Missing 'capability' in payload"}};
        }

        std::string capability = request["payload"]["capability"].get<std::string>();
        auto server = reg->bestServerFor(capability);

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

        Logger::getInstance().log("Forwarding to " + server->name + " at " + server->url);

        auto resp = client->post(server->url + "/mcp", forwardReq.dump(),
                                  headers, server->timeoutSeconds);

        if (resp.statusCode != 200) {
            return {{"status", "error"},
                    {"error", "Remote server returned " + std::to_string(resp.statusCode)},
                    {"server", server->name}};
        }

        try {
            auto parsed = nlohmann::json::parse(resp.body);
            parsed["_routed_to"] = server->name;
            return parsed;
        } catch (...) {
            return {{"status", "error"}, {"error", "Invalid response from remote server"}};
        }
    });
}
