#include "skills/SkillCommand.h"
#include "core/Logger.h"

SkillCommand::SkillCommand(std::shared_ptr<SkillEngine> engine,
                             std::shared_ptr<ILLMProvider> provider)
    : engine_(std::move(engine)), provider_(std::move(provider)) {}

std::future<nlohmann::json> SkillCommand::executeAsync(const nlohmann::json& request) {
    auto eng = engine_;
    auto prov = provider_;

    return std::async(std::launch::async, [eng, prov, request]() -> nlohmann::json {
        if (!request.contains("payload") || !request["payload"].contains("skill")) {
            return {{"status", "error"}, {"error", "Missing 'skill' in payload"}};
        }

        std::string skillName = request["payload"]["skill"].get<std::string>();
        auto skillOpt = eng->resolve(skillName);

        if (!skillOpt.has_value()) {
            return {{"status", "error"},
                    {"error", "Unknown skill: " + skillName},
                    {"available_skills", eng->listSkills()}};
        }

        const auto& skill = skillOpt.value();
        nlohmann::json variables = request["payload"].value("variables", nlohmann::json::object());

        std::string prompt;
        try {
            prompt = eng->renderPrompt(skill, variables);
        } catch (const std::invalid_argument& e) {
            return {{"status", "error"}, {"error", e.what()}};
        }

        // Use model from request, skill default, or provider default
        std::string model = request["payload"].value("model", skill.defaultModel);

        LLMRequest llmReq;
        llmReq.model = model;
        llmReq.messages = nlohmann::json::array({
            {{"role", "user"}, {"content", prompt}}
        });
        llmReq.parameters = skill.defaultParameters;

        // Override with request-level parameters if provided
        if (request["payload"].contains("parameters")) {
            for (const auto& [k, v] : request["payload"]["parameters"].items()) {
                llmReq.parameters[k] = v;
            }
        }

        auto future = prov->complete(llmReq);
        auto resp = future.get();

        return nlohmann::json{
            {"status", "ok"},
            {"skill", skillName},
            {"content", resp.content},
            {"model", model},
            {"input_tokens", resp.inputTokens},
            {"output_tokens", resp.outputTokens}
        };
    });
}
