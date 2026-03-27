#pragma once
#include <commands/ICommandStrategy.h>
#include <skills/SkillEngine.h>
#include <llm/ILLMProvider.h>
#include <memory>

class SkillCommand : public ICommandStrategy {
public:
    SkillCommand(std::shared_ptr<SkillEngine> engine,
                 std::shared_ptr<ILLMProvider> provider);
    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) override;
    ToolMetadata GetMetadata() const override;

private:
    std::shared_ptr<SkillEngine> m_Engine;
    std::shared_ptr<ILLMProvider> m_Provider;
};
