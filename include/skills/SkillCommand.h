#pragma once
#include "commands/ICommandStrategy.h"
#include "skills/SkillEngine.h"
#include "llm/ILLMProvider.h"
#include <memory>

class SkillCommand : public ICommandStrategy {
public:
    SkillCommand(std::shared_ptr<SkillEngine> engine,
                 std::shared_ptr<ILLMProvider> provider);
    std::future<nlohmann::json> executeAsync(const nlohmann::json& request) override;

private:
    std::shared_ptr<SkillEngine> engine_;
    std::shared_ptr<ILLMProvider> provider_;
};
