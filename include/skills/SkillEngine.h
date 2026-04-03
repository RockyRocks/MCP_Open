#pragma once
#include <skills/SkillDefinition.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class SkillEngine {
public:
    void LoadFromDirectory(const std::string& skillsDir);
    void LoadSkill(const SkillDefinition& skill);
    std::optional<SkillDefinition> Resolve(const std::string& name) const;
    std::vector<std::string> ListSkills() const;
    nlohmann::json ListSkillsJson() const;
    std::string RenderPrompt(const SkillDefinition& skill,
                             const nlohmann::json& variables) const;

    /// Static variant — usable without an engine instance (e.g. SkillToolAdapter).
    static std::string StaticRenderPrompt(const SkillDefinition& skill,
                                          const nlohmann::json& variables);

private:
    std::unordered_map<std::string, SkillDefinition> m_Skills;
    static std::string Interpolate(const std::string& templ, const nlohmann::json& vars);
    static std::string SanitizeVariable(const std::string& value);
};
