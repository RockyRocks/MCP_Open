#include <skills/SkillCommand.h>
#include <core/Logger.h>

SkillCommand::SkillCommand(std::shared_ptr<SkillEngine> engine,
                             std::shared_ptr<ILLMProvider> provider)
    : m_Engine(std::move(engine)), m_Provider(std::move(provider)) {}

std::future<nlohmann::json> SkillCommand::ExecuteAsync(const nlohmann::json& request) {
    auto eng = m_Engine;
    auto prov = m_Provider;

    return std::async(std::launch::async, [eng, prov, request]() -> nlohmann::json {
        if (!request.contains("payload") || !request["payload"].contains("skill")) {
            return {{"status", "error"}, {"error", "Missing 'skill' in payload"}};
        }

        std::string skillName = request["payload"]["skill"].get<std::string>();
        auto skillOpt = eng->Resolve(skillName);

        if (!skillOpt.has_value()) {
            return {{"status", "error"},
                    {"error", "Unknown skill: " + skillName},
                    {"available_skills", eng->ListSkills()}};
        }

        const auto& skill = skillOpt.value();
        nlohmann::json variables = request["payload"].value("variables", nlohmann::json::object());

        std::string prompt;
        try {
            prompt = eng->RenderPrompt(skill, variables);
        } catch (const std::invalid_argument& e) {
            return {{"status", "error"}, {"error", e.what()}};
        }

        // Use model from request, skill default, or provider default
        std::string model = request["payload"].value("model", skill.m_DefaultModel);

        LLMRequest llmReq;
        llmReq.m_Model = model;

        // Build messages with optional system prompt + rules
        nlohmann::json messages = nlohmann::json::array();

        std::string systemContent;
        if (!skill.m_SystemPrompt.empty()) {
            systemContent = skill.m_SystemPrompt;
        }
        if (!skill.m_Rules.empty()) {
            if (!systemContent.empty()) systemContent += "\n\n";
            systemContent += "Rules:\n";
            for (size_t i = 0; i < skill.m_Rules.size(); ++i) {
                systemContent += std::to_string(i + 1) + ". " + skill.m_Rules[i] + "\n";
            }
        }
        if (!systemContent.empty()) {
            messages.push_back({{"role", "system"}, {"content", systemContent}});
        }
        messages.push_back({{"role", "user"}, {"content", prompt}});

        llmReq.m_Messages = messages;
        llmReq.m_Parameters = skill.m_DefaultParameters;

        // Override with request-level parameters if provided
        if (request["payload"].contains("parameters")) {
            for (const auto& [k, v] : request["payload"]["parameters"].items()) {
                llmReq.m_Parameters[k] = v;
            }
        }

        auto future = prov->Complete(llmReq);
        auto resp = future.get();

        return nlohmann::json{
            {"status", "ok"},
            {"skill", skillName},
            {"content", resp.m_Content},
            {"model", model},
            {"input_tokens", resp.m_InputTokens},
            {"output_tokens", resp.m_OutputTokens}
        };
    });
}

ToolMetadata SkillCommand::GetMetadata() const {
    ToolMetadata meta;
    meta.m_Name = "skill";
    meta.m_Description = "Execute a skill prompt template with variables (legacy meta-tool — prefer calling skill tools directly)";
    meta.m_InputSchema = {
        {"type", "object"},
        {"properties", {
            {"skill", {{"type", "string"}, {"description", "Skill name to execute"}}},
            {"variables", {{"type", "object"}, {"description", "Template variables to interpolate"}}},
            {"model", {{"type", "string"}, {"description", "Override the skill's default model"}}},
            {"parameters", {{"type", "object"}, {"description", "Override the skill's default parameters"}}}
        }},
        {"required", nlohmann::json::array({"skill"})}
    };
    meta.m_Source = ToolSource::BuiltIn;
    meta.m_Hidden = true; // Skills are promoted as individual first-class tools
    return meta;
}
