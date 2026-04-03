#include <skills/PluginLoader.h>
#include <core/Logger.h>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// Read the entire content of a file into a string.
std::string ReadFile(const fs::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path.string());
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

/// Split a SKILL.md string into {frontmatter, body}.
/// Returns false if the file does not start with "---".
bool SplitFrontmatter(const std::string& content,
                      std::string& outFrontmatter,
                      std::string& outBody) {
    if (content.size() < 3 || content.substr(0, 3) != "---") {
        return false;
    }

    // Find the closing ---
    auto closingPos = content.find("\n---", 3);
    if (closingPos == std::string::npos) {
        return false;
    }

    outFrontmatter = content.substr(3, closingPos - 3);
    // Body starts after the closing --- line
    auto bodyStart = content.find('\n', closingPos + 4);
    outBody = (bodyStart == std::string::npos) ? "" : content.substr(bodyStart + 1);
    return true;
}

/// Parse simple YAML key:value lines and "- item" lists.
/// Only handles the subset used in SKILL.md frontmatter.
struct FrontmatterData {
    std::string name;
    std::string description;
    std::vector<std::string> variables;
};

FrontmatterData ParseFrontmatter(const std::string& yaml) {
    FrontmatterData data;
    std::istringstream stream(yaml);
    std::string line;
    bool inVariables = false;

    while (std::getline(stream, line)) {
        // Strip \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            inVariables = false;
            continue;
        }

        // List item under "variables:"
        if (inVariables && line.size() >= 2 && line[0] == '-' && line[1] == ' ') {
            std::string item = line.substr(2);
            // Trim leading/trailing whitespace
            auto start = item.find_first_not_of(' ');
            auto end = item.find_last_not_of(' ');
            if (start != std::string::npos) {
                data.variables.push_back(item.substr(start, end - start + 1));
            }
            continue;
        }

        inVariables = false;

        auto colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;

        std::string key = line.substr(0, colonPos);
        std::string value = (colonPos + 1 < line.size()) ? line.substr(colonPos + 1) : "";

        // Trim whitespace from key and value
        auto trimStart = [](const std::string& s) {
            auto p = s.find_first_not_of(" \t");
            return (p == std::string::npos) ? "" : s.substr(p);
        };
        auto trimEnd = [](const std::string& s) {
            auto p = s.find_last_not_of(" \t");
            return (p == std::string::npos) ? "" : s.substr(0, p + 1);
        };

        key = trimEnd(trimStart(key));
        value = trimEnd(trimStart(value));

        if (key == "name") {
            data.name = value;
        } else if (key == "description") {
            data.description = value;
        } else if (key == "variables") {
            inVariables = true; // next lines with "- " are variables
        }
    }

    return data;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// PluginLoader public API
// ---------------------------------------------------------------------------

SkillDefinition PluginLoader::ParseSkillMd(const std::string& content,
                                            const std::string& fallbackName) {
    std::string frontmatter;
    std::string body;

    if (!SplitFrontmatter(content, frontmatter, body)) {
        throw std::runtime_error("SKILL.md has no valid YAML frontmatter");
    }

    auto fm = ParseFrontmatter(frontmatter);

    SkillDefinition skill;
    skill.m_Name = fm.name.empty() ? fallbackName : fm.name;
    skill.m_Description = fm.description;
    skill.m_PromptTemplate = body;
    skill.m_RequiredVariables = fm.variables;
    // Plugins are LLM-agnostic — no default model baked in; resolved at runtime
    skill.m_DefaultModel = "";

    if (skill.m_Name.empty()) {
        throw std::runtime_error("SKILL.md has no 'name' field and no fallback name was given");
    }

    return skill;
}

void PluginLoader::LoadIntoEngine(const std::string& pluginsDir, SkillEngine& engine) {
    if (!fs::exists(pluginsDir) || !fs::is_directory(pluginsDir)) {
        Logger::GetInstance().Log("Plugins directory not found: " + pluginsDir + " (skipping)");
        return;
    }

    int loaded = 0;
    for (const auto& pluginEntry : fs::directory_iterator(pluginsDir)) {
        if (!pluginEntry.is_directory()) continue;

        fs::path pluginDir = pluginEntry.path();
        fs::path skillsDir = pluginDir / "skills";

        if (!fs::exists(skillsDir) || !fs::is_directory(skillsDir)) continue;

        for (const auto& skillEntry : fs::directory_iterator(skillsDir)) {
            if (!skillEntry.is_directory()) continue;

            fs::path skillMdPath = skillEntry.path() / "SKILL.md";
            if (!fs::exists(skillMdPath)) continue;

            std::string fallbackName = skillEntry.path().filename().string();

            try {
                std::string content = ReadFile(skillMdPath);
                SkillDefinition skill = ParseSkillMd(content, fallbackName);
                engine.LoadSkill(skill);
                ++loaded;
                Logger::GetInstance().Log("Loaded plugin skill: " + skill.m_Name
                                          + " (from " + skillMdPath.string() + ")");
            } catch (const std::exception& e) {
                Logger::GetInstance().Log("Failed to load plugin skill from "
                                          + skillMdPath.string() + ": " + e.what());
            }
        }
    }

    Logger::GetInstance().Log("PluginLoader: loaded " + std::to_string(loaded) + " plugin skill(s)");
}
