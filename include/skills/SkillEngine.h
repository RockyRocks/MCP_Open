#pragma once
#include "skills/SkillDefinition.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class SkillEngine {
public:
    void loadFromDirectory(const std::string& skillsDir);
    void loadSkill(const SkillDefinition& skill);
    std::optional<SkillDefinition> resolve(const std::string& name) const;
    std::vector<std::string> listSkills() const;
    nlohmann::json listSkillsJson() const;
    std::string renderPrompt(const SkillDefinition& skill,
                             const nlohmann::json& variables) const;

private:
    std::unordered_map<std::string, SkillDefinition> skills_;
    static std::string interpolate(const std::string& templ, const nlohmann::json& vars);
    static std::string sanitizeVariable(const std::string& value);
};
