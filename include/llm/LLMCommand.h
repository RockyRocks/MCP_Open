#pragma once
#include "commands/ICommandStrategy.h"
#include "llm/ILLMProvider.h"
#include <memory>

class LLMCommand : public ICommandStrategy {
public:
    explicit LLMCommand(std::shared_ptr<ILLMProvider> provider);
    std::future<nlohmann::json> executeAsync(const nlohmann::json& request) override;
    ToolMetadata metadata() const override;

private:
    std::shared_ptr<ILLMProvider> provider_;
};
