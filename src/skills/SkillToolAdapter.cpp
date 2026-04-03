#include <skills/SkillToolAdapter.h>
#include <skills/SkillEngine.h>
#include <core/Logger.h>

SkillToolAdapter::SkillToolAdapter(SkillDefinition def,
                                   std::shared_ptr<ILLMProvider> provider,
                                   ToolSource source)
    : m_Def(std::move(def)), m_Provider(std::move(provider)), m_Source(source) {}

std::future<nlohmann::json> SkillToolAdapter::ExecuteAsync(const nlohmann::json& request) {
    auto def = m_Def;
    auto provider = m_Provider;

    return std::async(std::launch::async, [def, provider, request]() -> nlohmann::json {
        // payload IS the variables (StdioTransport injects arguments directly as payload)
        nlohmann::json variables = request.value("payload", nlohmann::json::object());

        std::string prompt;
        try {
            prompt = SkillEngine::StaticRenderPrompt(def, variables);
        } catch (const std::invalid_argument& e) {
            return {{"status", "error"}, {"error", e.what()}};
        }

        std::string model = variables.value("model", def.m_DefaultModel);

        LLMRequest llmReq;
        llmReq.m_Model = model;

        nlohmann::json messages = nlohmann::json::array();
        std::string systemContent;
        if (!def.m_SystemPrompt.empty()) {
            systemContent = def.m_SystemPrompt;
        }
        if (!def.m_Rules.empty()) {
            if (!systemContent.empty()) systemContent += "\n\n";
            systemContent += "Rules:\n";
            for (size_t i = 0; i < def.m_Rules.size(); ++i) {
                systemContent += std::to_string(i + 1) + ". " + def.m_Rules[i] + "\n";
            }
        }
        if (!systemContent.empty()) {
            messages.push_back({{"role", "system"}, {"content", systemContent}});
        }
        messages.push_back({{"role", "user"}, {"content", prompt}});

        llmReq.m_Messages = messages;
        llmReq.m_Parameters = def.m_DefaultParameters;

        if (request.contains("payload") && request["payload"].contains("parameters")) {
            for (const auto& [k, v] : request["payload"]["parameters"].items()) {
                llmReq.m_Parameters[k] = v;
            }
        }

        try {
            auto future = provider->Complete(llmReq);
            auto resp = future.get();
            return nlohmann::json{
                {"status", "ok"},
                {"skill", def.m_Name},
                {"content", resp.m_Content},
                {"model", model},
                {"input_tokens", resp.m_InputTokens},
                {"output_tokens", resp.m_OutputTokens}
            };
        } catch (const std::exception& e) {
            Logger::GetInstance().Log(std::string("SkillToolAdapter LLM error: ") + e.what());
            return {{"status", "error"}, {"error", e.what()}};
        }
    });
}

ToolMetadata SkillToolAdapter::GetMetadata() const {
    // Build inputSchema from required_variables + optional common overrides
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json required = nlohmann::json::array();

    for (const auto& var : m_Def.m_RequiredVariables) {
        properties[var] = {{"type", "string"}, {"description", "Required: " + var}};
        required.push_back(var);
    }
    // Allow callers to override model and parameters per-call
    properties["model"] = {{"type", "string"}, {"description", "Override the default LLM model"}};
    properties["parameters"] = {{"type", "object"}, {"description", "Override default LLM parameters (temperature, max_tokens, ...)"}};

    nlohmann::json schema = {
        {"type", "object"},
        {"properties", properties},
        {"required", required}
    };

    ToolMetadata meta;
    meta.m_Name = m_Def.m_Name;
    meta.m_Description = m_Def.m_Description;
    meta.m_InputSchema = std::move(schema);
    meta.m_DefaultModel = m_Def.m_DefaultModel;
    meta.m_DefaultParameters = m_Def.m_DefaultParameters;
    meta.m_Source = m_Source;
    meta.m_Hidden = false;
    return meta;
}
