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

ToolMetadata LLMCommand::metadata() const {
    return {
        "llm",
        "Send a prompt to an LLM via LiteLLM proxy",
        {
            {"type", "object"},
            {"properties", {
                {"model", {{"type", "string"}, {"description", "Model name (e.g. claude-sonnet, gpt-4o)"}}},
                {"prompt", {{"type", "string"}, {"description", "Simple text prompt (convenience alternative to messages)"}}},
                {"messages", {{"type", "array"}, {"description", "Chat messages array [{role, content}]"},
                    {"items", {{"type", "object"}}}}},
                {"parameters", {{"type", "object"}, {"description", "Model parameters (temperature, max_tokens, etc.)"}}}
            }}
        },
        "",  // no default model — caller chooses
        {}   // no default parameters
    };
}
