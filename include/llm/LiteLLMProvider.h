#pragma once
#include <llm/ILLMProvider.h>
#include <http/IHttpClient.h>
#include <core/Config.h>
#include <memory>

class LiteLLMProvider : public ILLMProvider {
public:
    LiteLLMProvider(std::shared_ptr<IHttpClient> httpClient, const Config& config);

    std::future<LLMResponse> Complete(const LLMRequest& request) override;
    std::string GetProviderName() const override;
    bool IsAvailable() const override;

private:
    std::shared_ptr<IHttpClient> m_HttpClient;
    std::string m_BaseUrl;
    std::string m_DefaultModel;
};
