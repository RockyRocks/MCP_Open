#pragma once
#include <skills/SkillEngine.h>
#include <string>

/// Loads Claude Code plugins into a SkillEngine.
///
/// Expected directory layout:
///   plugins/
///     <plugin-name>/
///       plugin.json          (required — name, description, keywords)
///       skills/
///         <skill-name>/
///           SKILL.md         (required — YAML frontmatter + Markdown body)
///           scripts/         (optional — executable scripts)
///           references/      (optional — reference docs)
///           assets/          (optional — template files)
///
/// SKILL.md frontmatter keys recognised:
///   name:        skill name (falls back to directory name)
///   description: one-line description (required for good tool discovery)
///   variables:   YAML list of required variable names
///
/// The Markdown body after the closing --- becomes the prompt_template,
/// preserving {{variable}} placeholders compatible with SkillEngine::Interpolate.
class PluginLoader {
public:
    /// Walk pluginsDir and load every SKILL.md into engine.
    /// Silently skips missing or malformed entries (logs warnings).
    static void LoadIntoEngine(const std::string& pluginsDir, SkillEngine& engine);

    /// Parse a SKILL.md string into a SkillDefinition.
    /// Throws std::runtime_error if the content has no valid frontmatter.
    static SkillDefinition ParseSkillMd(const std::string& content,
                                        const std::string& fallbackName = "");
};
