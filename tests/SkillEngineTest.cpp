#include <gtest/gtest.h>
#include "skills/SkillEngine.h"

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
