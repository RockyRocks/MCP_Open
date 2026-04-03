#pragma once
#include <commands/ICommandStrategy.h>
#include <llm/ILLMProvider.h>
#include <skills/SkillDefinition.h>
#include <memory>

/// Wraps a SkillDefinition as a first-class ICommandStrategy so each skill
/// is exposed as its own named MCP tool (e.g. "code_review") rather than
/// being dispatched through the generic "skill" meta-tool.
///
/// The tool's inputSchema is built from the skill's required_variables, so
/// any LLM can call it directly without knowing about the skill layer.
class SkillToolAdapter : public ICommandStrategy {
public:
    SkillToolAdapter(SkillDefinition def,
                     std::shared_ptr<ILLMProvider> provider,
                     ToolSource source = ToolSource::JsonSkill);

    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) override;
    ToolMetadata GetMetadata() const override;

private:
    SkillDefinition m_Def;
    std::shared_ptr<ILLMProvider> m_Provider;
    ToolSource m_Source;
};
