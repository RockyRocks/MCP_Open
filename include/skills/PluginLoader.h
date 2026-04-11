#pragma once
#include <skills/SkillEngine.h>
#include <string>

/// Loads Claude Code plugins into a SkillEngine.
///
/// Expected directory layout:
///   plugins/
///     <plugin-name>/
///       plugin.json          (optional — name, description, keywords)
///       skills/
///         <skill-name>/
///           SKILL.md         (required — YAML frontmatter + Markdown body)
///           references/      (optional — CLI reference and prerequisite docs)
///       scripts/             (optional — executable binaries, e.g. es.exe)
///
/// SKILL.md frontmatter keys recognised:
///   name:             skill name (falls back to directory name)
///   description:      one-line description (required for good tool discovery)
///   variables:        YAML list of required variable names
///   type:             "llm" (default) | "command"
///   command_template: shell command with {{variable}} placeholders;
///                     ${PLUGIN_DIR} is replaced with the absolute plugin directory
///   rules:            YAML list of usage rules; for command skills these are
///                     appended to the tool description so the LLM sees them
///
/// For LLM skills the Markdown body becomes prompt_template.
/// For command skills the body is stored as documentation only;
/// execution uses command_template.
class PluginLoader {
public:
    /// Walk pluginsDir and load every SKILL.md into engine.
    /// Silently skips missing or malformed entries (logs warnings).
    static void LoadIntoEngine(const std::string& pluginsDir, SkillEngine& engine);

    /// Parse a SKILL.md string into a SkillDefinition.
    /// Throws std::runtime_error if the content has no valid frontmatter.
    /// pluginDir is substituted for ${PLUGIN_DIR} in command_template.
    static SkillDefinition ParseSkillMd(const std::string& content,
                                        const std::string& fallbackName = "",
                                        const std::string& pluginDir    = "");
};
