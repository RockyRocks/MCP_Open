#pragma once
#include "llm/ILLMProvider.h"
#include "http/IHttpClient.h"
#include "core/Config.h"
#include <memory>

class LiteLLMProvider : public ILLMProvider {
public:
    LiteLLMProvider(std::shared_ptr<IHttpClient> httpClient, const Config& config);

    std::future<LLMResponse> complete(const LLMRequest& request) override;
    std::string providerName() const override;
    bool isAvailable() const override;

private:
    std::shared_ptr<IHttpClient> httpClient_;
    std::string baseUrl_;
    std::string defaultModel_;
};
