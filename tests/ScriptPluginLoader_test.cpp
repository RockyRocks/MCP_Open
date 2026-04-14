#include <gtest/gtest.h>
#include <plugins/ScriptPluginLoader.h>
#include <commands/CommandRegistry.h>
#include <commands/ToolMetadata.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Python availability helper
// ---------------------------------------------------------------------------

static bool IsPythonAvailable() {
#ifdef _WIN32
    FILE* p = _popen("python --version 2>NUL", "r");
#else
    FILE* p = popen("python3 --version 2>/dev/null", "r");
#endif
    if (!p) return false;
    char buf[64];
    bool got = (fgets(buf, sizeof(buf), p) != nullptr);
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    return got;
}

#ifdef _WIN32
static const std::string kPythonRuntime = "python";
#else
static const std::string kPythonRuntime = "python3";
#endif

static const std::string kTestPluginDir = TEST_PLUGIN_DIR;

// ---------------------------------------------------------------------------
// Fixture: creates a fresh temp directory per test, tears it down afterwards
// ---------------------------------------------------------------------------

class ScriptPluginLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string dirName = std::string("mcp_loader_test_")
            + info->test_suite_name() + "_" + info->name();
        // Replace characters that are invalid in directory names
        for (char& c : dirName) {
            if (c == ' ' || c == '/' || c == '\\' || c == ':') c = '_';
        }
        m_TempDir = fs::temp_directory_path() / dirName;
        std::error_code ec;
        fs::remove_all(m_TempDir, ec);  // clean any leftover from a prior run
        fs::create_directories(m_TempDir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_TempDir, ec);
    }

    /// Write a plugin.json into <m_TempDir>/<pluginDirName>/plugin.json
    void CreatePluginJson(const std::string& pluginDirName,
                          const nlohmann::json& pluginJson) {
        fs::path dir = m_TempDir / pluginDirName;
        fs::create_directories(dir);
        std::ofstream f(dir / "plugin.json");
        f << pluginJson.dump(2);
    }

    /// Write a text file at the given absolute path (creates parent dirs).
    void CreateFile(const fs::path& path, const std::string& content) {
        fs::create_directories(path.parent_path());
        std::ofstream f(path);
        f << content;
    }

    fs::path m_TempDir;
};

// ===========================================================================
// Structural tests — no Python required
// ===========================================================================

TEST_F(ScriptPluginLoaderTest, LoadAll_NonExistentDir_DoesNotCrash) {
    CommandRegistry reg;
    // Should silently return, not throw
    EXPECT_NO_THROW(ScriptPluginLoader::LoadAll("/nonexistent/plugins/dir", reg));
    EXPECT_EQ(reg.ListCommands().size(), 0u);
}

TEST_F(ScriptPluginLoaderTest, LoadAll_EmptyDir_RegistersNothing) {
    CommandRegistry reg;
    ScriptPluginLoader::LoadAll(m_TempDir.string(), reg);
    EXPECT_EQ(reg.ListCommands().size(), 0u);
}

TEST_F(ScriptPluginLoaderTest, LoadAll_NoPluginJson_Skipped) {
    // Subdirectory without plugin.json — should be silently skipped
    fs::create_directories(m_TempDir / "no-json-plugin");

    CommandRegistry reg;
    ScriptPluginLoader::LoadAll(m_TempDir.string(), reg);
    EXPECT_EQ(reg.ListCommands().size(), 0u);
}

TEST_F(ScriptPluginLoaderTest, LoadAll_PluginJsonWithoutRuntimeKey_Skipped) {
    // Native plugin pattern: plugin.json without "runtime" key
    CreatePluginJson("native-style", {
        {"name", "native-style"},
        {"version", "1.0.0"},
        {"description", "No runtime key — treated as native plugin"}
    });

    CommandRegistry reg;
    ScriptPluginLoader::LoadAll(m_TempDir.string(), reg);
    EXPECT_EQ(reg.ListCommands().size(), 0u);
}

TEST_F(ScriptPluginLoaderTest, LoadAll_MissingEntrypoint_Skipped) {
    // plugin.json declares a runtime and entrypoint path that does not exist
    CreatePluginJson("broken-plugin", {
        {"name",       "broken-plugin"},
        {"runtime",    kPythonRuntime},
        {"entrypoint", "scripts/missing_plugin.py"}
    });

    CommandRegistry reg;
    EXPECT_NO_THROW(ScriptPluginLoader::LoadAll(m_TempDir.string(), reg));
    EXPECT_EQ(reg.ListCommands().size(), 0u);
}

TEST_F(ScriptPluginLoaderTest, LoadAll_MalformedJson_DoesNotCrash) {
    // Create a plugin dir with a plugin.json that is not valid JSON
    fs::path pluginDir = m_TempDir / "malformed-plugin";
    fs::create_directories(pluginDir);
    std::ofstream f(pluginDir / "plugin.json");
    f << "{ this is not valid json !!!";

    CommandRegistry reg;
    EXPECT_NO_THROW(ScriptPluginLoader::LoadAll(m_TempDir.string(), reg));
    EXPECT_EQ(reg.ListCommands().size(), 0u);
}

TEST_F(ScriptPluginLoaderTest, LoadAll_EntrypointNotFound_Skipped) {
    // plugin.json is valid and has "runtime", but the referenced entrypoint is absent
    CreatePluginJson("absent-script", {
        {"name",       "absent-script"},
        {"version",    "1.0.0"},
        {"runtime",    kPythonRuntime},
        {"entrypoint", "scripts/does_not_exist.py"}
    });

    CommandRegistry reg;
    EXPECT_NO_THROW(ScriptPluginLoader::LoadAll(m_TempDir.string(), reg));
    EXPECT_EQ(reg.ListCommands().size(), 0u);
}

// ===========================================================================
// Integration tests — require Python; skipped if not installed
// ===========================================================================

TEST_F(ScriptPluginLoaderTest, LoadAll_EchoPlugin_RegistersTwoTools) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    CommandRegistry reg;
    ScriptPluginLoader::LoadAll(kTestPluginDir, reg);

    // The echo-plugin contributes echo_tool and fail_tool
    EXPECT_TRUE(reg.HasCommand("echo_tool"));
    EXPECT_TRUE(reg.HasCommand("fail_tool"));
}

TEST_F(ScriptPluginLoaderTest, LoadAll_EchoPlugin_ToolsHaveScriptPluginSource) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    CommandRegistry reg;
    ScriptPluginLoader::LoadAll(kTestPluginDir, reg);

    for (const auto& meta : reg.ListToolMetadata()) {
        EXPECT_EQ(meta.m_Source, ToolSource::ScriptPlugin)
            << "Tool '" << meta.m_Name << "' has unexpected source";
    }
}

TEST_F(ScriptPluginLoaderTest, LoadAll_EchoPlugin_ToolExecutable) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    CommandRegistry reg;
    ScriptPluginLoader::LoadAll(kTestPluginDir, reg);

    ASSERT_TRUE(reg.HasCommand("echo_tool")) << "echo_tool not registered";

    auto cmd = reg.Resolve("echo_tool");
    ASSERT_NE(cmd, nullptr);

    nlohmann::json request = {
        {"command", "echo_tool"},
        {"payload", {{"message", "loader test"}}}
    };
    auto result = cmd->ExecuteAsync(request).get();

    EXPECT_FALSE(result.value("isError", false));
    ASSERT_TRUE(result.contains("content"));
    ASSERT_FALSE(result["content"].empty());
    std::string text = result["content"][0].value("text", "");
    EXPECT_NE(text.find("loader test"), std::string::npos);
}

TEST_F(ScriptPluginLoaderTest, LoadAll_TwoScriptPlugins_BothRegistered) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    // Create two independent script plugins, each providing a unique tool name

    // Plugin A — "ping_tool"
    CreatePluginJson("plugin-a", {
        {"name",       "plugin-a"},
        {"version",    "1.0.0"},
        {"runtime",    kPythonRuntime},
        {"entrypoint", "scripts/plugin_a.py"}
    });
    CreateFile(m_TempDir / "plugin-a" / "scripts" / "plugin_a.py",
        "import sys, json\n"
        "import argparse\n"
        "parser = argparse.ArgumentParser(add_help=False)\n"
        "parser.add_argument('--mcp-list', action='store_true')\n"
        "parser.add_argument('--mcp-call', metavar='TOOL')\n"
        "parser.add_argument('--mcp-args-file', metavar='FILE')\n"
        "args, _ = parser.parse_known_args()\n"
        "if args.mcp_list:\n"
        "    sys.stdout.write(json.dumps([{\"name\":\"ping_tool\","
            "\"description\":\"ping\","
            "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}]) + \"\\n\")\n"
        "elif args.mcp_call == 'ping_tool':\n"
        "    sys.stdout.write(json.dumps({\"status\":\"ok\",\"content\":\"pong\"}) + \"\\n\")\n"
    );

    // Plugin B — "pong_tool"
    CreatePluginJson("plugin-b", {
        {"name",       "plugin-b"},
        {"version",    "1.0.0"},
        {"runtime",    kPythonRuntime},
        {"entrypoint", "scripts/plugin_b.py"}
    });
    CreateFile(m_TempDir / "plugin-b" / "scripts" / "plugin_b.py",
        "import sys, json\n"
        "import argparse\n"
        "parser = argparse.ArgumentParser(add_help=False)\n"
        "parser.add_argument('--mcp-list', action='store_true')\n"
        "parser.add_argument('--mcp-call', metavar='TOOL')\n"
        "parser.add_argument('--mcp-args-file', metavar='FILE')\n"
        "args, _ = parser.parse_known_args()\n"
        "if args.mcp_list:\n"
        "    sys.stdout.write(json.dumps([{\"name\":\"pong_tool\","
            "\"description\":\"pong\","
            "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}]) + \"\\n\")\n"
        "elif args.mcp_call == 'pong_tool':\n"
        "    sys.stdout.write(json.dumps({\"status\":\"ok\",\"content\":\"ping\"}) + \"\\n\")\n"
    );

    CommandRegistry reg;
    ScriptPluginLoader::LoadAll(m_TempDir.string(), reg);

    EXPECT_TRUE(reg.HasCommand("ping_tool")) << "ping_tool from plugin-a not registered";
    EXPECT_TRUE(reg.HasCommand("pong_tool")) << "pong_tool from plugin-b not registered";
}
