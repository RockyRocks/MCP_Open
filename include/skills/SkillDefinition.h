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
};
