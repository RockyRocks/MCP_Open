#include <gtest/gtest.h>
#include "commands/CommandRegistry.h"
#include "commands/EchoCommand.h"

TEST(CommandRegistryTest, RegisterAndResolve) {
    CommandRegistry reg;
    reg.registerCommand("echo", createEchoCommand());
    EXPECT_NE(reg.resolve("echo"), nullptr);
}

TEST(CommandRegistryTest, ResolveUnknown) {
    CommandRegistry reg;
    EXPECT_EQ(reg.resolve("nonexistent"), nullptr);
}

TEST(CommandRegistryTest, HasCommand) {
    CommandRegistry reg;
    reg.registerCommand("echo", createEchoCommand());
    EXPECT_TRUE(reg.hasCommand("echo"));
    EXPECT_FALSE(reg.hasCommand("nonexistent"));
}

TEST(CommandRegistryTest, ListCommands) {
    CommandRegistry reg;
    reg.registerCommand("echo", createEchoCommand());
    reg.registerCommand("test", createEchoCommand());
    auto cmds = reg.listCommands();
    EXPECT_EQ(cmds.size(), 2u);
}

TEST(CommandRegistryTest, EmptyNameThrows) {
    CommandRegistry reg;
    EXPECT_THROW(reg.registerCommand("", createEchoCommand()), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// listToolMetadata() tests
// ---------------------------------------------------------------------------

TEST(CommandRegistryTest, MetadataFromSelfDescribingCommand) {
    CommandRegistry reg;
    reg.registerCommand("echo", createEchoCommand());

    auto metadata = reg.listToolMetadata();
    ASSERT_EQ(metadata.size(), 1u);

    auto& meta = metadata[0];
    EXPECT_EQ(meta.name, "echo");
    EXPECT_EQ(meta.description, "Echo back the input message");
    EXPECT_TRUE(meta.inputSchema.contains("properties"));
    EXPECT_TRUE(meta.inputSchema["properties"].contains("message"));
}

namespace {

// Bare command that does NOT override metadata()
class BareCommand : public ICommandStrategy {
public:
    std::future<nlohmann::json> executeAsync(const nlohmann::json& request) override {
        return std::async(std::launch::async, [request]() {
            return nlohmann::json{{"status", "ok"}};
        });
    }
};

// Command with custom defaultModel and defaultParameters
class ModeledCommand : public ICommandStrategy {
public:
    std::future<nlohmann::json> executeAsync(const nlohmann::json& request) override {
        return std::async(std::launch::async, [request]() {
            return nlohmann::json{{"status", "ok"}};
        });
    }

    ToolMetadata metadata() const override {
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
    reg.registerCommand("bare_tool", std::make_shared<BareCommand>());

    auto metadata = reg.listToolMetadata();
    ASSERT_EQ(metadata.size(), 1u);

    auto& meta = metadata[0];
    EXPECT_EQ(meta.name, "bare_tool");
    EXPECT_EQ(meta.description, "Execute the bare_tool command");
    EXPECT_TRUE(meta.inputSchema.contains("additionalProperties"));
    EXPECT_TRUE(meta.inputSchema["additionalProperties"].get<bool>());
}

TEST(CommandRegistryTest, DefaultModelAndParametersPassThrough) {
    CommandRegistry reg;
    reg.registerCommand("modeled", std::make_shared<ModeledCommand>());

    auto metadata = reg.listToolMetadata();
    ASSERT_EQ(metadata.size(), 1u);

    auto& meta = metadata[0];
    EXPECT_EQ(meta.name, "modeled");
    EXPECT_EQ(meta.defaultModel, "claude-opus");
    EXPECT_EQ(meta.defaultParameters["temperature"], 0.1);
    EXPECT_EQ(meta.defaultParameters["max_tokens"], 512);
}
