#include <llm/LLMCommand.h>

LLMCommand::LLMCommand(std::shared_ptr<ILLMProvider> provider)
    : m_Provider(std::move(provider)) {}

std::future<nlohmann::json> LLMCommand::ExecuteAsync(const nlohmann::json& request) {
    auto prov = m_Provider;

    return std::async(std::launch::async, [prov, request]() -> nlohmann::json {
        LLMRequest llmReq;

        if (request.contains("payload")) {
            const auto& payload = request["payload"];
            if (payload.contains("model"))
                llmReq.m_Model = payload["model"].get<std::string>();
            if (payload.contains("messages"))
                llmReq.m_Messages = payload["messages"];
            if (payload.contains("parameters"))
                llmReq.m_Parameters = payload["parameters"];

            // Convenience: if "prompt" is provided instead of "messages", wrap it
            if (!payload.contains("messages") && payload.contains("prompt")) {
                llmReq.m_Messages = nlohmann::json::array({
                    {{"role", "user"}, {"content", payload["prompt"].get<std::string>()}}
                });
            }
        }

        auto future = prov->Complete(llmReq);
        auto resp = future.get();

        return nlohmann::json{
            {"status", "ok"},
            {"content", resp.m_Content},
            {"input_tokens", resp.m_InputTokens},
            {"output_tokens", resp.m_OutputTokens},
            {"finish_reason", resp.m_FinishReason}
        };
    });
}

ToolMetadata LLMCommand::GetMetadata() const {
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
