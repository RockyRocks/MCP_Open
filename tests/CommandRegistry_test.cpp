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
