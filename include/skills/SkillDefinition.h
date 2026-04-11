#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

enum class SkillType {
    LLM,     // default — sends prompt to LLM provider
    Command  // executes command_template as a shell command, returns stdout
};

struct SkillDefinition {
    std::string m_Name;
    std::string m_Description;
    std::string m_PromptTemplate;
    std::string m_DefaultModel;
    nlohmann::json m_DefaultParameters;
    std::vector<std::string> m_RequiredVariables;
    std::string m_SystemPrompt;          // system-level instruction for the LLM
    std::vector<std::string> m_Rules;    // execution rules/preferences
    SkillType   m_Type            = SkillType::LLM;
    std::string m_CommandTemplate;       // used when m_Type == Command
};
