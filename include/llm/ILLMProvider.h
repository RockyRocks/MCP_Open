#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <future>
#include <vector>

struct LLMRequest {
    std::string model;
    nlohmann::json messages;        // [{"role":"user","content":"..."}]
    nlohmann::json parameters;      // {"temperature":0.7, "max_tokens":1024}
};

struct LLMResponse {
    std::string content;
    int inputTokens = 0;
    int outputTokens = 0;
    std::string finishReason;
    nlohmann::json raw;
};

class ILLMProvider {
public:
    virtual ~ILLMProvider() = default;
    virtual std::future<LLMResponse> complete(const LLMRequest& request) = 0;
    virtual std::string providerName() const = 0;
    virtual bool isAvailable() const = 0;
};
