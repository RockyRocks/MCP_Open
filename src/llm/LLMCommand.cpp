#include "llm/LLMCommand.h"

LLMCommand::LLMCommand(std::shared_ptr<ILLMProvider> provider)
    : provider_(std::move(provider)) {}

std::future<nlohmann::json> LLMCommand::executeAsync(const nlohmann::json& request) {
    auto prov = provider_;

    return std::async(std::launch::async, [prov, request]() -> nlohmann::json {
        LLMRequest llmReq;

        if (request.contains("payload")) {
            const auto& payload = request["payload"];
            if (payload.contains("model"))
                llmReq.model = payload["model"].get<std::string>();
            if (payload.contains("messages"))
                llmReq.messages = payload["messages"];
            if (payload.contains("parameters"))
                llmReq.parameters = payload["parameters"];

            // Convenience: if "prompt" is provided instead of "messages", wrap it
            if (!payload.contains("messages") && payload.contains("prompt")) {
                llmReq.messages = nlohmann::json::array({
                    {{"role", "user"}, {"content", payload["prompt"].get<std::string>()}}
                });
            }
        }

        auto future = prov->complete(llmReq);
        auto resp = future.get();

        return nlohmann::json{
            {"status", "ok"},
            {"content", resp.content},
            {"input_tokens", resp.inputTokens},
            {"output_tokens", resp.outputTokens},
            {"finish_reason", resp.finishReason}
        };
    });
}
