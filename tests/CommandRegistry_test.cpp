#include <gtest/gtest.h>
#include <commands/CommandRegistry.h>
#include <commands/EchoCommand.h>

TEST(CommandRegistryTest, RegisterAndResolve) {
    CommandRegistry reg;
    reg.RegisterCommand("echo", CreateEchoCommand());
    EXPECT_NE(reg.Resolve("echo"), nullptr);
}

TEST(CommandRegistryTest, ResolveUnknown) {
    CommandRegistry reg;
    EXPECT_EQ(reg.Resolve("nonexistent"), nullptr);
}

TEST(CommandRegistryTest, HasCommand) {
    CommandRegistry reg;
    reg.RegisterCommand("echo", CreateEchoCommand());
    EXPECT_TRUE(reg.HasCommand("echo"));
    EXPECT_FALSE(reg.HasCommand("nonexistent"));
}

TEST(CommandRegistryTest, ListCommands) {
    CommandRegistry reg;
    reg.RegisterCommand("echo", CreateEchoCommand());
    reg.RegisterCommand("test", CreateEchoCommand());
    auto cmds = reg.ListCommands();
    EXPECT_EQ(cmds.size(), 2u);
}

TEST(CommandRegistryTest, EmptyNameThrows) {
    CommandRegistry reg;
    EXPECT_THROW(reg.RegisterCommand("", CreateEchoCommand()), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// ListToolMetadata() tests
// ---------------------------------------------------------------------------

TEST(CommandRegistryTest, MetadataFromSelfDescribingCommand) {
    CommandRegistry reg;
    reg.RegisterCommand("echo", CreateEchoCommand());

    auto metadata = reg.ListToolMetadata();
    ASSERT_EQ(metadata.size(), 1u);

    auto& meta = metadata[0];
    EXPECT_EQ(meta.m_Name, "echo");
    EXPECT_EQ(meta.m_Description, "Echo back the input message");
    EXPECT_TRUE(meta.m_InputSchema.contains("properties"));
    EXPECT_TRUE(meta.m_InputSchema["properties"].contains("message"));
}

namespace {

// Bare command that provides minimal metadata
class BareCommand : public ICommandStrategy {
public:
    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) override {
        return std::async(std::launch::async, [request]() {
            return nlohmann::json{{"status", "ok"}};
        });
    }

    ToolMetadata GetMetadata() const override {
        return {};
    }
};

// Command with custom defaultModel and defaultParameters
class ModeledCommand : public ICommandStrategy {
public:
    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json& request) override {
        return std::async(std::launch::async, [request]() {
            return nlohmann::json{{"status", "ok"}};
        });
    }

    ToolMetadata GetMetadata() const override {
        return {
            "modeled",
            "A command with a default model",
            {
                {"type", "object"},
                {"properties", {
                    {"input", {{"type", "string"}}}
                }}
            },
            "claude-opus",
            {{"temperature", 0.1}, {"max_tokens", 512}}
        };
    }
};

} // anonymous namespace

TEST(CommandRegistryTest, FallbackMetadataForCommandWithoutOverride) {
    CommandRegistry reg;
    reg.RegisterCommand("bare_tool", std::make_shared<BareCommand>());

    auto metadata = reg.ListToolMetadata();
    ASSERT_EQ(metadata.size(), 1u);

    auto& meta = metadata[0];
    EXPECT_EQ(meta.m_Name, "bare_tool");
    EXPECT_EQ(meta.m_Description, "Execute the bare_tool command");
    EXPECT_TRUE(meta.m_InputSchema.contains("additionalProperties"));
    EXPECT_TRUE(meta.m_InputSchema["additionalProperties"].get<bool>());
}

TEST(CommandRegistryTest, DefaultModelAndParametersPassThrough) {
    CommandRegistry reg;
    reg.RegisterCommand("modeled", std::make_shared<ModeledCommand>());

    auto metadata = reg.ListToolMetadata();
    ASSERT_EQ(metadata.size(), 1u);

    auto& meta = metadata[0];
    EXPECT_EQ(meta.m_Name, "modeled");
    EXPECT_EQ(meta.m_DefaultModel, "claude-opus");
    EXPECT_EQ(meta.m_DefaultParameters["temperature"], 0.1);
    EXPECT_EQ(meta.m_DefaultParameters["max_tokens"], 512);
}

// ---------------------------------------------------------------------------
// Hidden tool filtering tests
// ---------------------------------------------------------------------------

namespace {

class HiddenCommand : public ICommandStrategy {
public:
    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json&) override {
        return std::async(std::launch::async, []() {
            return nlohmann::json{{"status", "ok"}};
        });
    }

    ToolMetadata GetMetadata() const override {
        ToolMetadata meta;
        meta.m_Name = "hidden_tool";
        meta.m_Description = "This tool is hidden";
        meta.m_Hidden = true;
        return meta;
    }
};

class SkillSourceCommand : public ICommandStrategy {
public:
    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json&) override {
        return std::async(std::launch::async, []() {
            return nlohmann::json{{"status", "ok"}};
        });
    }

    ToolMetadata GetMetadata() const override {
        ToolMetadata meta;
        meta.m_Name = "my_skill";
        meta.m_Description = "A promoted skill tool";
        meta.m_Source = ToolSource::JsonSkill;
        return meta;
    }
};

class PluginSourceCommand : public ICommandStrategy {
public:
    std::future<nlohmann::json> ExecuteAsync(const nlohmann::json&) override {
        return std::async(std::launch::async, []() {
            return nlohmann::json{{"status", "ok"}};
        });
    }

    ToolMetadata GetMetadata() const override {
        ToolMetadata meta;
        meta.m_Name = "plugin_tool";
        meta.m_Description = "A plugin tool";
        meta.m_Source = ToolSource::Plugin;
        return meta;
    }
};

} // anonymous namespace

TEST(CommandRegistryTest, HiddenToolExcludedFromListToolMetadata) {
    CommandRegistry reg;
    reg.RegisterCommand("echo", CreateEchoCommand());
    reg.RegisterCommand("hidden_tool", std::make_shared<HiddenCommand>());

    auto metadata = reg.ListToolMetadata();
    // Only echo should appear; hidden_tool is filtered out
    ASSERT_EQ(metadata.size(), 1u);
    EXPECT_EQ(metadata[0].m_Name, "echo");
}

TEST(CommandRegistryTest, HiddenToolIsStillResolvable) {
    // Hidden only means "omit from tools/list", not "refuse to execute"
    CommandRegistry reg;
    reg.RegisterCommand("hidden_tool", std::make_shared<HiddenCommand>());

    EXPECT_NE(reg.Resolve("hidden_tool"), nullptr);
    EXPECT_TRUE(reg.HasCommand("hidden_tool"));
}

TEST(CommandRegistryTest, ListCommandsIncludesHiddenTools) {
    // ListCommands() is the internal debug list — includes everything
    CommandRegistry reg;
    reg.RegisterCommand("visible", CreateEchoCommand());
    reg.RegisterCommand("hidden_tool", std::make_shared<HiddenCommand>());

    auto cmds = reg.ListCommands();
    EXPECT_EQ(cmds.size(), 2u);
}

TEST(CommandRegistryTest, ToolSourceDefaultsToBuiltIn) {
    CommandRegistry reg;
    reg.RegisterCommand("bare_tool", std::make_shared<BareCommand>());

    auto metadata = reg.ListToolMetadata();
    ASSERT_EQ(metadata.size(), 1u);
    EXPECT_EQ(metadata[0].m_Source, ToolSource::BuiltIn);
}

TEST(CommandRegistryTest, JsonSkillSourcePreserved) {
    CommandRegistry reg;
    reg.RegisterCommand("my_skill", std::make_shared<SkillSourceCommand>());

    auto metadata = reg.ListToolMetadata();
    ASSERT_EQ(metadata.size(), 1u);
    EXPECT_EQ(metadata[0].m_Source, ToolSource::JsonSkill);
}

TEST(CommandRegistryTest, PluginSourcePreserved) {
    CommandRegistry reg;
    reg.RegisterCommand("plugin_tool", std::make_shared<PluginSourceCommand>());

    auto metadata = reg.ListToolMetadata();
    ASSERT_EQ(metadata.size(), 1u);
    EXPECT_EQ(metadata[0].m_Source, ToolSource::Plugin);
}

TEST(CommandRegistryTest, MixedVisibleAndHiddenTools) {
    CommandRegistry reg;
    reg.RegisterCommand("echo", CreateEchoCommand());
    reg.RegisterCommand("hidden1", std::make_shared<HiddenCommand>());
    reg.RegisterCommand("skill_tool", std::make_shared<SkillSourceCommand>());

    auto metadata = reg.ListToolMetadata();
    // echo + skill_tool visible; hidden1 filtered
    EXPECT_EQ(metadata.size(), 2u);
    for (const auto& m : metadata) {
        EXPECT_NE(m.m_Name, "hidden1");
    }
}
