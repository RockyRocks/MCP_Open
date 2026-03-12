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
