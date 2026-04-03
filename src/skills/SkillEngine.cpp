#include <skills/SkillEngine.h>
#include <core/Logger.h>
#include <fstream>
#include <filesystem>
#include <regex>
#include <stdexcept>

namespace fs = std::filesystem;

void SkillEngine::LoadFromDirectory(const std::string& skillsDir) {
    if (!fs::exists(skillsDir) || !fs::is_directory(skillsDir)) {
        Logger::GetInstance().Log("Skills directory not found: " + skillsDir);
        return;
    }

    for (const auto& entry : fs::directory_iterator(skillsDir)) {
        if (entry.path().extension() != ".json") continue;

        try {
            std::ifstream file(entry.path());
            auto data = nlohmann::json::parse(file);

            SkillDefinition skill;
            skill.m_Name = data.value("name", "");
            skill.m_Description = data.value("description", "");
            skill.m_PromptTemplate = data.value("prompt_template", "");
            skill.m_DefaultModel = data.value("default_model", "");
            skill.m_DefaultParameters = data.value("default_parameters", nlohmann::json::object());

            if (data.contains("required_variables") && data["required_variables"].is_array()) {
                for (const auto& v : data["required_variables"]) {
                    skill.m_RequiredVariables.push_back(v.get<std::string>());
                }
            }

            skill.m_SystemPrompt = data.value("system_prompt", "");
            if (data.contains("rules") && data["rules"].is_array()) {
                for (const auto& r : data["rules"]) {
                    skill.m_Rules.push_back(r.get<std::string>());
                }
            }

            if (!skill.m_Name.empty() && !skill.m_PromptTemplate.empty()) {
                m_Skills[skill.m_Name] = std::move(skill);
                Logger::GetInstance().Log("Loaded skill: " + data["name"].get<std::string>());
            }
        } catch (const std::exception& e) {
            Logger::GetInstance().Log("Failed to load skill from " +
                                       entry.path().string() + ": " + e.what());
        }
    }

    Logger::GetInstance().Log("Loaded " + std::to_string(m_Skills.size()) + " skills");
}

void SkillEngine::LoadSkill(const SkillDefinition& skill) {
    if (skill.m_Name.empty()) {
        throw std::invalid_argument("Skill name cannot be empty");
    }
    m_Skills[skill.m_Name] = skill;
}

std::optional<SkillDefinition> SkillEngine::Resolve(const std::string& name) const {
    auto it = m_Skills.find(name);
    if (it == m_Skills.end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> SkillEngine::ListSkills() const {
    std::vector<std::string> names;
    names.reserve(m_Skills.size());
    for (const auto& [name, _] : m_Skills) {
        names.push_back(name);
    }
    return names;
}

nlohmann::json SkillEngine::ListSkillsJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [name, skill] : m_Skills) {
        arr.push_back({
            {"name", skill.m_Name},
            {"description", skill.m_Description},
            {"default_model", skill.m_DefaultModel},
            {"required_variables", skill.m_RequiredVariables}
        });
    }
    return arr;
}

std::string SkillEngine::RenderPrompt(const SkillDefinition& skill,
                                       const nlohmann::json& variables) const {
    return StaticRenderPrompt(skill, variables);
}

std::string SkillEngine::StaticRenderPrompt(const SkillDefinition& skill,
                                             const nlohmann::json& variables) {
    for (const auto& required : skill.m_RequiredVariables) {
        if (!variables.contains(required)) {
            throw std::invalid_argument("Missing required variable: " + required);
        }
    }
    return Interpolate(skill.m_PromptTemplate, variables);
}

std::string SkillEngine::SanitizeVariable(const std::string& value) {
    // Strip {{ and }} to prevent template injection
    std::string result = value;
    std::string::size_type pos;
    while ((pos = result.find("{{")) != std::string::npos) {
        result.erase(pos, 2);
    }
    while ((pos = result.find("}}")) != std::string::npos) {
        result.erase(pos, 2);
    }
    return result;
}

std::string SkillEngine::Interpolate(const std::string& templ, const nlohmann::json& vars) {
    std::string result = templ;
    std::regex placeholder(R"(\{\{(\w+)\}\})");
    std::smatch match;
    std::string working = result;
    result.clear();

    auto it = working.cbegin();
    while (std::regex_search(it, working.cend(), match, placeholder)) {
        result.append(it, it + match.position());
        std::string varName = match[1].str();
        if (vars.contains(varName)) {
            std::string val = vars[varName].is_string()
                                  ? vars[varName].get<std::string>()
                                  : vars[varName].dump();
            result += SanitizeVariable(val);
        } else {
            result += match[0].str(); // Leave placeholder if not provided
        }
        it += match.position() + match.length();
    }
    result.append(it, working.cend());

    return result;
}
