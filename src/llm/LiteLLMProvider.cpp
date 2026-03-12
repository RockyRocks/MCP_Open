#include "llm/LiteLLMProvider.h"
#include "core/Logger.h"

LiteLLMProvider::LiteLLMProvider(std::shared_ptr<IHttpClient> httpClient, const Config& config)
    : httpClient_(std::move(httpClient))
    , baseUrl_(config.litellmBaseUrl())
    , defaultModel_(config.defaultModel()) {}

std::future<LLMResponse> LiteLLMProvider::complete(const LLMRequest& request) {
    auto client = httpClient_;
    auto url = baseUrl_;
    auto model = request.model.empty() ? defaultModel_ : request.model;

    return std::async(std::launch::async, [client, url, model, request]() -> LLMResponse {
        // Build OpenAI-compatible request
        nlohmann::json reqBody = {
            {"model", model},
            {"messages", request.messages}
        };

        // Merge parameters
        if (request.parameters.is_object()) {
            for (const auto& [key, val] : request.parameters.items()) {
                reqBody[key] = val;
            }
        }

        std::unordered_map<std::string, std::string> headers = {
            {"Content-Type", "application/json"}
        };

        auto httpResp = client->post(url + "/v1/chat/completions", reqBody.dump(), headers, 60);

        LLMResponse response;
        if (httpResp.statusCode != 200) {
            Logger::getInstance().log("LiteLLM request failed with status " +
                                       std::to_string(httpResp.statusCode));
            response.content = "Error: LiteLLM returned status " +
                               std::to_string(httpResp.statusCode);
            response.raw = {{"error", httpResp.body}};
            return response;
        }

        try {
            auto parsed = nlohmann::json::parse(httpResp.body);
            response.raw = parsed;

            if (parsed.contains("choices") && !parsed["choices"].empty()) {
                auto& choice = parsed["choices"][0];
                if (choice.contains("message") && choice["message"].contains("content")) {
                    response.content = choice["message"]["content"].get<std::string>();
                }
                if (choice.contains("finish_reason")) {
                    response.finishReason = choice["finish_reason"].get<std::string>();
                }
            }

            if (parsed.contains("usage")) {
                auto& usage = parsed["usage"];
                if (usage.contains("prompt_tokens"))
                    response.inputTokens = usage["prompt_tokens"].get<int>();
                if (usage.contains("completion_tokens"))
                    response.outputTokens = usage["completion_tokens"].get<int>();
            }
        } catch (const std::exception& e) {
            Logger::getInstance().log(std::string("Failed to parse LiteLLM response: ") + e.what());
            response.content = "Error: Failed to parse response";
        }

        return response;
    });
}

std::string LiteLLMProvider::providerName() const {
    return "litellm";
}

bool LiteLLMProvider::isAvailable() const {
    try {
        auto resp = httpClient_->get(baseUrl_ + "/health", {}, 5);
        return resp.statusCode == 200;
    } catch (...) {
        return false;
    }
}
