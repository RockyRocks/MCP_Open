#pragma once
#include <commands/ICommandStrategy.h>
#include <llm/ILLMProvider.h>
#include <memory>

class LLMCommand : public ICommandStrategy {
public:
    explicit LLMCommand(std::shared_ptr<ILLMProvider> provider);
    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) override;
    ToolMetadata GetMetadata() const override;

private:
    std::shared_ptr<ILLMProvider> m_Provider;
};
