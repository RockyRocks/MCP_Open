#include "skills/SkillEngine.h"
#include "core/Logger.h"
#include <fstream>
#include <filesystem>
#include <regex>
#include <stdexcept>

namespace fs = std::filesystem;

void SkillEngine::loadFromDirectory(const std::string& skillsDir) {
    if (!fs::exists(skillsDir) || !fs::is_directory(skillsDir)) {
        Logger::getInstance().log("Skills directory not found: " + skillsDir);
        return;
    }

    for (const auto& entry : fs::directory_iterator(skillsDir)) {
        if (entry.path().extension() != ".json") continue;

        try {
            std::ifstream file(entry.path());
            auto data = nlohmann::json::parse(file);

            SkillDefinition skill;
            skill.name = data.value("name", "");
            skill.description = data.value("description", "");
            skill.promptTemplate = data.value("prompt_template", "");
            skill.defaultModel = data.value("default_model", "");
            skill.defaultParameters = data.value("default_parameters", nlohmann::json::object());

            if (data.contains("required_variables") && data["required_variables"].is_array()) {
                for (const auto& v : data["required_variables"]) {
                    skill.requiredVariables.push_back(v.get<std::string>());
                }
            }

            skill.systemPrompt = data.value("system_prompt", "");
            if (data.contains("rules") && data["rules"].is_array()) {
                for (const auto& r : data["rules"]) {
                    skill.rules.push_back(r.get<std::string>());
                }
            }

            if (!skill.name.empty() && !skill.promptTemplate.empty()) {
                skills_[skill.name] = std::move(skill);
                Logger::getInstance().log("Loaded skill: " + data["name"].get<std::string>());
            }
        } catch (const std::exception& e) {
            Logger::getInstance().log("Failed to load skill from " +
                                       entry.path().string() + ": " + e.what());
        }
    }

    Logger::getInstance().log("Loaded " + std::to_string(skills_.size()) + " skills");
}

void SkillEngine::loadSkill(const SkillDefinition& skill) {
    if (skill.name.empty()) {
        throw std::invalid_argument("Skill name cannot be empty");
    }
    skills_[skill.name] = skill;
}

std::optional<SkillDefinition> SkillEngine::resolve(const std::string& name) const {
    auto it = skills_.find(name);
    if (it == skills_.end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> SkillEngine::listSkills() const {
    std::vector<std::string> names;
    names.reserve(skills_.size());
    for (const auto& [name, _] : skills_) {
        names.push_back(name);
    }
    return names;
}

nlohmann::json SkillEngine::listSkillsJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [name, skill] : skills_) {
        arr.push_back({
            {"name", skill.name},
            {"description", skill.description},
            {"default_model", skill.defaultModel},
            {"required_variables", skill.requiredVariables}
        });
    }
    return arr;
}

std::string SkillEngine::renderPrompt(const SkillDefinition& skill,
                                       const nlohmann::json& variables) const {
    // Validate required variables
    for (const auto& required : skill.requiredVariables) {
        if (!variables.contains(required)) {
            throw std::invalid_argument("Missing required variable: " + required);
        }
    }

    return interpolate(skill.promptTemplate, variables);
}

std::string SkillEngine::sanitizeVariable(const std::string& value) {
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

std::string SkillEngine::interpolate(const std::string& templ, const nlohmann::json& vars) {
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
            result += sanitizeVariable(val);
        } else {
            result += match[0].str(); // Leave placeholder if not provided
        }
        it += match.position() + match.length();
    }
    result.append(it, working.cend());

    return result;
}
