#include <llm/LiteLLMProvider.h>
#include <core/Logger.h>

LiteLLMProvider::LiteLLMProvider(std::shared_ptr<IHttpClient> httpClient, const Config& config)
    : m_HttpClient(std::move(httpClient))
    , m_BaseUrl(config.GetLiteLlmBaseUrl())
    , m_DefaultModel(config.GetDefaultModel()) {}

std::future<LLMResponse> LiteLLMProvider::Complete(const LLMRequest& request) {
    auto client = m_HttpClient;
    auto url = m_BaseUrl;
    auto model = request.m_Model.empty() ? m_DefaultModel : request.m_Model;

    return std::async(std::launch::async, [client, url, model, request]() -> LLMResponse {
        // Build OpenAI-compatible request
        nlohmann::json reqBody = {
            {"model", model},
            {"messages", request.m_Messages}
        };

        // Merge parameters
        if (request.m_Parameters.is_object()) {
            for (const auto& [key, val] : request.m_Parameters.items()) {
                reqBody[key] = val;
            }
        }

        std::unordered_map<std::string, std::string> headers = {
            {"Content-Type", "application/json"}
        };

        auto httpResp = client->Post(url + "/v1/chat/completions", reqBody.dump(), headers, 60);

        LLMResponse response;
        if (httpResp.m_StatusCode != 200) {
            Logger::GetInstance().Log("LiteLLM request failed with status " +
                                       std::to_string(httpResp.m_StatusCode));
            response.m_Content = "Error: LiteLLM returned status " +
                               std::to_string(httpResp.m_StatusCode);
            response.m_Raw = {{"error", httpResp.m_Body}};
            return response;
        }

        try {
            auto parsed = nlohmann::json::parse(httpResp.m_Body);
            response.m_Raw = parsed;

            if (parsed.contains("choices") && !parsed["choices"].empty()) {
                auto& choice = parsed["choices"][0];
                if (choice.contains("message") && choice["message"].contains("content")) {
                    response.m_Content = choice["message"]["content"].get<std::string>();
                }
                if (choice.contains("finish_reason")) {
                    response.m_FinishReason = choice["finish_reason"].get<std::string>();
                }
            }

            if (parsed.contains("usage")) {
                auto& usage = parsed["usage"];
                if (usage.contains("prompt_tokens"))
                    response.m_InputTokens = usage["prompt_tokens"].get<int>();
                if (usage.contains("completion_tokens"))
                    response.m_OutputTokens = usage["completion_tokens"].get<int>();
            }
        } catch (const std::exception& e) {
            Logger::GetInstance().Log(std::string("Failed to parse LiteLLM response: ") + e.what());
            response.m_Content = "Error: Failed to parse response";
        }

        return response;
    });
}

std::string LiteLLMProvider::GetProviderName() const {
    return "litellm";
}

bool LiteLLMProvider::IsAvailable() const {
    try {
        auto resp = m_HttpClient->Get(m_BaseUrl + "/health", {}, 5);
        return resp.m_StatusCode == 200;
    } catch (...) {
        return false;
    }
}
