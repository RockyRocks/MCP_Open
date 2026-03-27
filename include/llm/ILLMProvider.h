#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <future>
#include <vector>

struct LLMRequest {
    std::string m_Model;
    nlohmann::json m_Messages;        // [{"role":"user","content":"..."}]
    nlohmann::json m_Parameters;      // {"temperature":0.7, "max_tokens":1024}
};

struct LLMResponse {
    std::string m_Content;
    int m_InputTokens = 0;
    int m_OutputTokens = 0;
    std::string m_FinishReason;
    nlohmann::json m_Raw;
};

class ILLMProvider {
public:
    virtual ~ILLMProvider() = default;
    virtual std::future<LLMResponse> Complete(const LLMRequest& request) = 0;
    virtual std::string GetProviderName() const = 0;
    virtual bool IsAvailable() const = 0;
};
