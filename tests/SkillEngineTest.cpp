#include <gtest/gtest.h>
#include "skills/SkillEngine.h"
#include "skills/SkillCommand.h"
#include "llm/ILLMProvider.h"

class SkillEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        SkillDefinition skill;
        skill.name = "summarize";
        skill.description = "Summarize text";
        skill.promptTemplate = "Summarize: {{input}} in {{num_points}} points.";
        skill.defaultModel = "claude-sonnet";
        skill.requiredVariables = {"input"};
        engine.loadSkill(skill);
    }

    SkillEngine engine;
};

TEST_F(SkillEngineTest, ResolveExisting) {
    auto skill = engine.resolve("summarize");
    ASSERT_TRUE(skill.has_value());
    EXPECT_EQ(skill->name, "summarize");
}

TEST_F(SkillEngineTest, ResolveNonexistent) {
    auto skill = engine.resolve("nonexistent");
    EXPECT_FALSE(skill.has_value());
}

TEST_F(SkillEngineTest, ListSkills) {
    auto skills = engine.listSkills();
    EXPECT_EQ(skills.size(), 1u);
    EXPECT_EQ(skills[0], "summarize");
}

TEST_F(SkillEngineTest, RenderPrompt) {
    auto skill = engine.resolve("summarize").value();
    nlohmann::json vars = {{"input", "test text"}, {"num_points", "3"}};
    std::string prompt = engine.renderPrompt(skill, vars);
    EXPECT_EQ(prompt, "Summarize: test text in 3 points.");
}

TEST_F(SkillEngineTest, RenderPromptMissingRequired) {
    auto skill = engine.resolve("summarize").value();
    nlohmann::json vars = {{"num_points", "3"}}; // missing "input"
    EXPECT_THROW(engine.renderPrompt(skill, vars), std::invalid_argument);
}

TEST_F(SkillEngineTest, RenderPromptTemplateInjection) {
    auto skill = engine.resolve("summarize").value();
    nlohmann::json vars = {{"input", "{{malicious}}"}, {"num_points", "3"}};
    std::string prompt = engine.renderPrompt(skill, vars);
    // {{ and }} should be stripped from user input
    EXPECT_EQ(prompt.find("{{malicious}}"), std::string::npos);
    EXPECT_NE(prompt.find("malicious"), std::string::npos);
}

TEST_F(SkillEngineTest, LoadSkillEmptyNameThrows) {
    SkillDefinition bad;
    bad.name = "";
    EXPECT_THROW(engine.loadSkill(bad), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// systemPrompt + rules tests
// ---------------------------------------------------------------------------

TEST_F(SkillEngineTest, SkillWithSystemPromptAndRules) {
    SkillDefinition skill;
    skill.name = "reviewer";
    skill.description = "Review code";
    skill.promptTemplate = "Review: {{code}}";
    skill.defaultModel = "claude-opus";
    skill.requiredVariables = {"code"};
    skill.systemPrompt = "You are a code reviewer.";
    skill.rules = {"Be concise", "Focus on bugs"};
    engine.loadSkill(skill);

    auto loaded = engine.resolve("reviewer");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->systemPrompt, "You are a code reviewer.");
    ASSERT_EQ(loaded->rules.size(), 2u);
    EXPECT_EQ(loaded->rules[0], "Be concise");
    EXPECT_EQ(loaded->rules[1], "Focus on bugs");
}

TEST_F(SkillEngineTest, SkillWithoutSystemPromptHasEmptyDefaults) {
    // The "summarize" skill from SetUp() has no systemPrompt/rules
    auto skill = engine.resolve("summarize");
    ASSERT_TRUE(skill.has_value());
    EXPECT_TRUE(skill->systemPrompt.empty());
    EXPECT_TRUE(skill->rules.empty());
}

// ---------------------------------------------------------------------------
// SkillCommand system message injection tests
// ---------------------------------------------------------------------------

namespace {

// Mock LLM provider that captures the request
class CapturingLLMProvider : public ILLMProvider {
public:
    mutable LLMRequest lastRequest;

    std::future<LLMResponse> complete(const LLMRequest& request) override {
        lastRequest = request;
        return std::async(std::launch::async, []() {
            return LLMResponse{"mock response", 10, 5, "stop", {}};
        });
    }

    std::string providerName() const override { return "mock"; }
    bool isAvailable() const override { return true; }
};

} // anonymous namespace

TEST(SkillCommandTest, SystemPromptInjected) {
    auto engine = std::make_shared<SkillEngine>();
    SkillDefinition skill;
    skill.name = "test_skill";
    skill.promptTemplate = "Do: {{input}}";
    skill.defaultModel = "test-model";
    skill.requiredVariables = {"input"};
    skill.systemPrompt = "You are a test assistant.";
    engine->loadSkill(skill);

    auto provider = std::make_shared<CapturingLLMProvider>();
    SkillCommand cmd(engine, provider);

    nlohmann::json request = {
        {"payload", {{"skill", "test_skill"}, {"variables", {{"input", "hello"}}}}}
    };
    auto result = cmd.executeAsync(request).get();

    EXPECT_EQ(result["status"], "ok");
    ASSERT_GE(provider->lastRequest.messages.size(), 2u);
    EXPECT_EQ(provider->lastRequest.messages[0]["role"], "system");
    EXPECT_NE(provider->lastRequest.messages[0]["content"].get<std::string>().find(
        "You are a test assistant."), std::string::npos);
    EXPECT_EQ(provider->lastRequest.messages[1]["role"], "user");
}

TEST(SkillCommandTest, RulesAppendedToSystemPrompt) {
    auto engine = std::make_shared<SkillEngine>();
    SkillDefinition skill;
    skill.name = "ruled_skill";
    skill.promptTemplate = "Do: {{input}}";
    skill.defaultModel = "test-model";
    skill.requiredVariables = {"input"};
    skill.systemPrompt = "Be helpful.";
    skill.rules = {"Always return JSON", "Keep it short"};
    engine->loadSkill(skill);

    auto provider = std::make_shared<CapturingLLMProvider>();
    SkillCommand cmd(engine, provider);

    nlohmann::json request = {
        {"payload", {{"skill", "ruled_skill"}, {"variables", {{"input", "test"}}}}}
    };
    auto result = cmd.executeAsync(request).get();

    auto systemMsg = provider->lastRequest.messages[0]["content"].get<std::string>();
    EXPECT_NE(systemMsg.find("Be helpful."), std::string::npos);
    EXPECT_NE(systemMsg.find("1. Always return JSON"), std::string::npos);
    EXPECT_NE(systemMsg.find("2. Keep it short"), std::string::npos);
}

TEST(SkillCommandTest, NoSystemMessageWhenEmpty) {
    auto engine = std::make_shared<SkillEngine>();
    SkillDefinition skill;
    skill.name = "plain_skill";
    skill.promptTemplate = "Do: {{input}}";
    skill.defaultModel = "test-model";
    skill.requiredVariables = {"input"};
    // No systemPrompt or rules
    engine->loadSkill(skill);

    auto provider = std::make_shared<CapturingLLMProvider>();
    SkillCommand cmd(engine, provider);

    nlohmann::json request = {
        {"payload", {{"skill", "plain_skill"}, {"variables", {{"input", "test"}}}}}
    };
    auto result = cmd.executeAsync(request).get();

    // Should have only a user message, no system message
    ASSERT_EQ(provider->lastRequest.messages.size(), 1u);
    EXPECT_EQ(provider->lastRequest.messages[0]["role"], "user");
}
