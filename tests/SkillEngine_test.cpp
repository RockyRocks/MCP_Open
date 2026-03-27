#include <gtest/gtest.h>
#include <skills/SkillEngine.h>
#include <skills/SkillCommand.h>
#include <llm/ILLMProvider.h>

class SkillEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        SkillDefinition skill;
        skill.m_Name = "summarize";
        skill.m_Description = "Summarize text";
        skill.m_PromptTemplate = "Summarize: {{input}} in {{num_points}} points.";
        skill.m_DefaultModel = "claude-sonnet";
        skill.m_RequiredVariables = {"input"};
        engine.LoadSkill(skill);
    }

    SkillEngine engine;
};

TEST_F(SkillEngineTest, ResolveExisting) {
    auto skill = engine.Resolve("summarize");
    ASSERT_TRUE(skill.has_value());
    EXPECT_EQ(skill->m_Name, "summarize");
}

TEST_F(SkillEngineTest, ResolveNonexistent) {
    auto skill = engine.Resolve("nonexistent");
    EXPECT_FALSE(skill.has_value());
}

TEST_F(SkillEngineTest, ListSkills) {
    auto skills = engine.ListSkills();
    EXPECT_EQ(skills.size(), 1u);
    EXPECT_EQ(skills[0], "summarize");
}

TEST_F(SkillEngineTest, RenderPrompt) {
    auto skill = engine.Resolve("summarize").value();
    nlohmann::json vars = {{"input", "test text"}, {"num_points", "3"}};
    std::string prompt = engine.RenderPrompt(skill, vars);
    EXPECT_EQ(prompt, "Summarize: test text in 3 points.");
}

TEST_F(SkillEngineTest, RenderPromptMissingRequired) {
    auto skill = engine.Resolve("summarize").value();
    nlohmann::json vars = {{"num_points", "3"}}; // missing "input"
    EXPECT_THROW(engine.RenderPrompt(skill, vars), std::invalid_argument);
}

TEST_F(SkillEngineTest, RenderPromptTemplateInjection) {
    auto skill = engine.Resolve("summarize").value();
    nlohmann::json vars = {{"input", "{{malicious}}"}, {"num_points", "3"}};
    std::string prompt = engine.RenderPrompt(skill, vars);
    // {{ and }} should be stripped from user input
    EXPECT_EQ(prompt.find("{{malicious}}"), std::string::npos);
    EXPECT_NE(prompt.find("malicious"), std::string::npos);
}

TEST_F(SkillEngineTest, LoadSkillEmptyNameThrows) {
    SkillDefinition bad;
    bad.m_Name = "";
    EXPECT_THROW(engine.LoadSkill(bad), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// systemPrompt + rules tests
// ---------------------------------------------------------------------------

TEST_F(SkillEngineTest, SkillWithSystemPromptAndRules) {
    SkillDefinition skill;
    skill.m_Name = "reviewer";
    skill.m_Description = "Review code";
    skill.m_PromptTemplate = "Review: {{code}}";
    skill.m_DefaultModel = "claude-opus";
    skill.m_RequiredVariables = {"code"};
    skill.m_SystemPrompt = "You are a code reviewer.";
    skill.m_Rules = {"Be concise", "Focus on bugs"};
    engine.LoadSkill(skill);

    auto loaded = engine.Resolve("reviewer");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->m_SystemPrompt, "You are a code reviewer.");
    ASSERT_EQ(loaded->m_Rules.size(), 2u);
    EXPECT_EQ(loaded->m_Rules[0], "Be concise");
    EXPECT_EQ(loaded->m_Rules[1], "Focus on bugs");
}

TEST_F(SkillEngineTest, SkillWithoutSystemPromptHasEmptyDefaults) {
    // The "summarize" skill from SetUp() has no systemPrompt/rules
    auto skill = engine.Resolve("summarize");
    ASSERT_TRUE(skill.has_value());
    EXPECT_TRUE(skill->m_SystemPrompt.empty());
    EXPECT_TRUE(skill->m_Rules.empty());
}

// ---------------------------------------------------------------------------
// SkillCommand system message injection tests
// ---------------------------------------------------------------------------

namespace {

// Mock LLM provider that captures the request
class CapturingLLMProvider : public ILLMProvider {
public:
    mutable LLMRequest lastRequest;

    std::future<LLMResponse> Complete(const LLMRequest& request) override {
        lastRequest = request;
        return std::async(std::launch::async, []() {
            return LLMResponse{"mock response", 10, 5, "stop", {}};
        });
    }

    std::string GetProviderName() const override { return "mock"; }
    bool IsAvailable() const override { return true; }
};

} // anonymous namespace

TEST(SkillCommandTest, SystemPromptInjected) {
    auto engine = std::make_shared<SkillEngine>();
    SkillDefinition skill;
    skill.m_Name = "test_skill";
    skill.m_PromptTemplate = "Do: {{input}}";
    skill.m_DefaultModel = "test-model";
    skill.m_RequiredVariables = {"input"};
    skill.m_SystemPrompt = "You are a test assistant.";
    engine->LoadSkill(skill);

    auto provider = std::make_shared<CapturingLLMProvider>();
    SkillCommand cmd(engine, provider);

    nlohmann::json request = {
        {"payload", {{"skill", "test_skill"}, {"variables", {{"input", "hello"}}}}}
    };
    auto result = cmd.ExecuteAsync(request).get();

    EXPECT_EQ(result["status"], "ok");
    ASSERT_GE(provider->lastRequest.m_Messages.size(), 2u);
    EXPECT_EQ(provider->lastRequest.m_Messages[0]["role"], "system");
    EXPECT_NE(provider->lastRequest.m_Messages[0]["content"].get<std::string>().find(
        "You are a test assistant."), std::string::npos);
    EXPECT_EQ(provider->lastRequest.m_Messages[1]["role"], "user");
}

TEST(SkillCommandTest, RulesAppendedToSystemPrompt) {
    auto engine = std::make_shared<SkillEngine>();
    SkillDefinition skill;
    skill.m_Name = "ruled_skill";
    skill.m_PromptTemplate = "Do: {{input}}";
    skill.m_DefaultModel = "test-model";
    skill.m_RequiredVariables = {"input"};
    skill.m_SystemPrompt = "Be helpful.";
    skill.m_Rules = {"Always return JSON", "Keep it short"};
    engine->LoadSkill(skill);

    auto provider = std::make_shared<CapturingLLMProvider>();
    SkillCommand cmd(engine, provider);

    nlohmann::json request = {
        {"payload", {{"skill", "ruled_skill"}, {"variables", {{"input", "test"}}}}}
    };
    auto result = cmd.ExecuteAsync(request).get();

    auto systemMsg = provider->lastRequest.m_Messages[0]["content"].get<std::string>();
    EXPECT_NE(systemMsg.find("Be helpful."), std::string::npos);
    EXPECT_NE(systemMsg.find("1. Always return JSON"), std::string::npos);
    EXPECT_NE(systemMsg.find("2. Keep it short"), std::string::npos);
}

TEST(SkillCommandTest, NoSystemMessageWhenEmpty) {
    auto engine = std::make_shared<SkillEngine>();
    SkillDefinition skill;
    skill.m_Name = "plain_skill";
    skill.m_PromptTemplate = "Do: {{input}}";
    skill.m_DefaultModel = "test-model";
    skill.m_RequiredVariables = {"input"};
    // No systemPrompt or rules
    engine->LoadSkill(skill);

    auto provider = std::make_shared<CapturingLLMProvider>();
    SkillCommand cmd(engine, provider);

    nlohmann::json request = {
        {"payload", {{"skill", "plain_skill"}, {"variables", {{"input", "test"}}}}}
    };
    auto result = cmd.ExecuteAsync(request).get();

    // Should have only a user message, no system message
    ASSERT_EQ(provider->lastRequest.m_Messages.size(), 1u);
    EXPECT_EQ(provider->lastRequest.m_Messages[0]["role"], "user");
}
