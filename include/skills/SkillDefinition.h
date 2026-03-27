#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct SkillDefinition {
    std::string m_Name;
    std::string m_Description;
    std::string m_PromptTemplate;
    std::string m_DefaultModel;
    nlohmann::json m_DefaultParameters;
    std::vector<std::string> m_RequiredVariables;
    std::string m_SystemPrompt;          // system-level instruction for the LLM
    std::vector<std::string> m_Rules;    // execution rules/preferences
};
