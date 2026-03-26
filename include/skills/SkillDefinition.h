#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct SkillDefinition {
    std::string name;
    std::string description;
    std::string promptTemplate;
    std::string defaultModel;
    nlohmann::json defaultParameters;
    std::vector<std::string> requiredVariables;
    std::string systemPrompt;          // system-level instruction for the LLM
    std::vector<std::string> rules;    // execution rules/preferences
};
