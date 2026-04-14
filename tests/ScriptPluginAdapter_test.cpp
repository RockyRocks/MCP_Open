#include <gtest/gtest.h>
#include <plugins/ScriptPluginAdapter.h>
#include <commands/ToolMetadata.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Python availability helper — used to conditionally skip execution tests
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

// Paths derived from the compile-time TEST_PLUGIN_DIR definition
static const std::string kEchoPluginScript =
    std::string(TEST_PLUGIN_DIR) + "/echo-plugin/scripts/echo_plugin.py";

#ifdef _WIN32
static const std::string kPythonRuntime = "python";
#else
static const std::string kPythonRuntime = "python3";
#endif

// ===========================================================================
// Static tests — no Python required
// ===========================================================================

TEST(ScriptPluginAdapterTest, GetRuntimeExecutable_Python) {
#ifdef _WIN32
    EXPECT_EQ(ScriptPluginAdapter::GetRuntimeExecutable("python"), "python");
#else
    EXPECT_EQ(ScriptPluginAdapter::GetRuntimeExecutable("python"), "python3");
#endif
}

TEST(ScriptPluginAdapterTest, GetRuntimeExecutable_Node) {
    EXPECT_EQ(ScriptPluginAdapter::GetRuntimeExecutable("node"), "node");
}

TEST(ScriptPluginAdapterTest, GetRuntimeExecutable_Dotnet) {
    EXPECT_EQ(ScriptPluginAdapter::GetRuntimeExecutable("dotnet"), "dotnet");
}

TEST(ScriptPluginAdapterTest, GetRuntimeExecutable_CustomString) {
    EXPECT_EQ(ScriptPluginAdapter::GetRuntimeExecutable("python3"),   "python3");
    EXPECT_EQ(ScriptPluginAdapter::GetRuntimeExecutable("node20"),    "node20");
    EXPECT_EQ(ScriptPluginAdapter::GetRuntimeExecutable("myruntime"), "myruntime");
}

TEST(ScriptPluginAdapterTest, BuildListCommand_ContainsMcpListFlag) {
    auto cmd = ScriptPluginAdapter::BuildListCommand("python", "/path/to/plugin.py");
    EXPECT_NE(cmd.find("--mcp-list"), std::string::npos);
}

TEST(ScriptPluginAdapterTest, BuildListCommand_ContainsEntrypoint) {
    auto cmd = ScriptPluginAdapter::BuildListCommand("python", "/path/to/plugin.py");
    EXPECT_NE(cmd.find("/path/to/plugin.py"), std::string::npos);
}

TEST(ScriptPluginAdapterTest, BuildCallCommand_ContainsMcpCallAndToolName) {
    auto cmd = ScriptPluginAdapter::BuildCallCommand(
        "python", "/my/plugin.py", "echo_tool", "/tmp/args.json");
    EXPECT_NE(cmd.find("--mcp-call"),  std::string::npos);
    EXPECT_NE(cmd.find("echo_tool"),   std::string::npos);
}

TEST(ScriptPluginAdapterTest, BuildCallCommand_ContainsMcpArgsFileFlag) {
    auto cmd = ScriptPluginAdapter::BuildCallCommand(
        "python", "/my/plugin.py", "echo_tool", "/tmp/args.json");
    EXPECT_NE(cmd.find("--mcp-args-file"), std::string::npos);
    EXPECT_NE(cmd.find("/tmp/args.json"),   std::string::npos);
}

TEST(ScriptPluginAdapterTest, BuildCallCommand_QuotesEntrypoint) {
    // Path with spaces must be double-quoted in the generated command string
    auto cmd = ScriptPluginAdapter::BuildCallCommand(
        "python", "/my plugin/script.py", "tool", "/tmp/args.json");
    EXPECT_NE(cmd.find("\"/my plugin/script.py\""), std::string::npos);
}

TEST(ScriptPluginAdapterTest, BuildListCommand_ExecutableRuntime_NoRuntimePrefix) {
    auto cmd = ScriptPluginAdapter::BuildListCommand("executable", "/path/to/my_tool");
    // The word "executable" must NOT appear as a prefix — the entrypoint is the exe
    EXPECT_EQ(cmd.find("executable "), std::string::npos);
    EXPECT_NE(cmd.find("/path/to/my_tool"), std::string::npos);
    EXPECT_NE(cmd.find("--mcp-list"),        std::string::npos);
}

TEST(ScriptPluginAdapterTest, IsValidToolName_ValidNames) {
    EXPECT_TRUE(ScriptPluginAdapter::IsValidToolName("echo_tool"));
    EXPECT_TRUE(ScriptPluginAdapter::IsValidToolName("my-tool"));
    EXPECT_TRUE(ScriptPluginAdapter::IsValidToolName("tool123"));
    EXPECT_TRUE(ScriptPluginAdapter::IsValidToolName("a"));
    EXPECT_TRUE(ScriptPluginAdapter::IsValidToolName("UPPER_CASE"));
    EXPECT_TRUE(ScriptPluginAdapter::IsValidToolName("mix-ed_NAME99"));
}

TEST(ScriptPluginAdapterTest, IsValidToolName_InvalidNames) {
    EXPECT_FALSE(ScriptPluginAdapter::IsValidToolName(""));           // empty
    EXPECT_FALSE(ScriptPluginAdapter::IsValidToolName("bad name"));   // space
    EXPECT_FALSE(ScriptPluginAdapter::IsValidToolName("foo;bar"));    // semicolon
    EXPECT_FALSE(ScriptPluginAdapter::IsValidToolName("../etc/passwd")); // path chars
    EXPECT_FALSE(ScriptPluginAdapter::IsValidToolName("foo.bar"));    // dot
    EXPECT_FALSE(ScriptPluginAdapter::IsValidToolName("tool!"));      // punctuation
}

TEST(ScriptPluginAdapterTest, GetMetadata_Source_IsScriptPlugin) {
    ScriptPluginToolInfo info;
    info.m_Name        = "my_tool";
    info.m_Description = "Test tool";
    // Leave m_InputSchema empty

    ScriptPluginAdapter adapter("test-plugin", "python", "/path/to/script.py", info);
    auto meta = adapter.GetMetadata();

    EXPECT_EQ(meta.m_Source, ToolSource::ScriptPlugin);
    EXPECT_EQ(meta.m_Name,   "my_tool");
}

TEST(ScriptPluginAdapterTest, GetMetadata_EmptySchema_GetsDefault) {
    ScriptPluginToolInfo info;
    info.m_Name        = "my_tool";
    info.m_Description = "Test tool";
    // m_InputSchema deliberately left as default (null/empty)

    ScriptPluginAdapter adapter("test-plugin", "python", "/path/to/script.py", info);
    auto meta = adapter.GetMetadata();

    EXPECT_FALSE(meta.m_InputSchema.is_null());
    ASSERT_TRUE(meta.m_InputSchema.contains("type"));
    EXPECT_EQ(meta.m_InputSchema["type"], "object");
    EXPECT_TRUE(meta.m_InputSchema.contains("properties"));
}

// ===========================================================================
// Execution tests — require Python; skipped if not installed
// ===========================================================================

TEST(ScriptPluginAdapterTest, DiscoverTools_ReturnsCorrectCount) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    auto tools = ScriptPluginAdapter::DiscoverTools(
        "echo-plugin", kPythonRuntime, kEchoPluginScript);
    EXPECT_EQ(tools.size(), 2u);
}

TEST(ScriptPluginAdapterTest, DiscoverTools_FirstToolHasSchema) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    auto tools = ScriptPluginAdapter::DiscoverTools(
        "echo-plugin", kPythonRuntime, kEchoPluginScript);
    ASSERT_FALSE(tools.empty());

    auto it = std::find_if(tools.begin(), tools.end(),
        [](const ScriptPluginToolInfo& t) { return t.m_Name == "echo_tool"; });
    ASSERT_NE(it, tools.end()) << "echo_tool not found in discovered tools";

    EXPECT_FALSE(it->m_InputSchema.is_null());
    EXPECT_TRUE(it->m_InputSchema.contains("properties"));
}

TEST(ScriptPluginAdapterTest, DiscoverTools_NonExistentEntrypoint_ReturnsEmpty) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    auto tools = ScriptPluginAdapter::DiscoverTools(
        "nonexistent-plugin", kPythonRuntime, "/nonexistent/path/plugin.py");
    EXPECT_TRUE(tools.empty());
}

TEST(ScriptPluginAdapterTest, DiscoverTools_InvalidJsonOutput_ReturnsEmpty) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    // A script that writes non-JSON to stdout
    fs::path tmpScript = fs::temp_directory_path() / "mcp_bad_json_plugin.py";
    {
        std::ofstream f(tmpScript);
        f << "import sys\nsys.stdout.write('not valid json\\n')\n";
    }
    auto tools = ScriptPluginAdapter::DiscoverTools(
        "bad-plugin", kPythonRuntime, tmpScript.string());
    std::error_code ec;
    fs::remove(tmpScript, ec);

    EXPECT_TRUE(tools.empty());
}

TEST(ScriptPluginAdapterTest, DiscoverTools_InvalidToolName_ToolSkipped) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    // Script with one valid name and one name containing a space
    fs::path tmpScript = fs::temp_directory_path() / "mcp_mixed_names_plugin.py";
    {
        std::ofstream f(tmpScript);
        f << "import sys, json\n"
          << "tools = [\n"
          << "    {\"name\": \"valid_tool\", \"description\": \"ok\"},\n"
          << "    {\"name\": \"bad tool!\", \"description\": \"invalid\"}\n"
          << "]\n"
          << "sys.stdout.write(json.dumps(tools) + \"\\n\")\n";
    }
    auto tools = ScriptPluginAdapter::DiscoverTools(
        "mixed-plugin", kPythonRuntime, tmpScript.string());
    std::error_code ec;
    fs::remove(tmpScript, ec);

    ASSERT_EQ(tools.size(), 1u);
    EXPECT_EQ(tools[0].m_Name, "valid_tool");
}

TEST(ScriptPluginAdapterTest, ExecuteAsync_EchoTool_ReturnsMessage) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    ScriptPluginToolInfo info;
    info.m_Name        = "echo_tool";
    info.m_Description = "Echoes the input message back";

    ScriptPluginAdapter adapter("echo-plugin", kPythonRuntime, kEchoPluginScript, info);
    nlohmann::json request = {
        {"command", "echo_tool"},
        {"payload", {{"message", "hello from test"}}}
    };

    auto result = adapter.ExecuteAsync(request).get();

    EXPECT_FALSE(result.value("isError", false));
    ASSERT_TRUE(result.contains("content"));
    ASSERT_TRUE(result["content"].is_array());
    ASSERT_FALSE(result["content"].empty());

    std::string text = result["content"][0].value("text", "");
    EXPECT_NE(text.find("hello from test"), std::string::npos);
}

TEST(ScriptPluginAdapterTest, ExecuteAsync_FailTool_ReturnsIsError) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    ScriptPluginToolInfo info;
    info.m_Name        = "fail_tool";
    info.m_Description = "Always returns an error response";

    ScriptPluginAdapter adapter("echo-plugin", kPythonRuntime, kEchoPluginScript, info);
    nlohmann::json request = {
        {"command", "fail_tool"},
        {"payload", nlohmann::json::object()}
    };

    auto result = adapter.ExecuteAsync(request).get();

    EXPECT_TRUE(result.value("isError", false));
    ASSERT_TRUE(result.contains("content"));
    ASSERT_FALSE(result["content"].empty());

    std::string text = result["content"][0].value("text", "");
    EXPECT_NE(text.find("fail_tool always fails"), std::string::npos);
}

TEST(ScriptPluginAdapterTest, ExecuteAsync_TempFileDeletedAfterCall) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    ScriptPluginToolInfo info;
    info.m_Name        = "echo_tool";
    info.m_Description = "Echo";

    ScriptPluginAdapter adapter("echo-plugin", kPythonRuntime, kEchoPluginScript, info);
    nlohmann::json request = {
        {"command", "echo_tool"},
        {"payload", {{"message", "temp file cleanup test"}}}
    };

    // Snapshot how many mcp_script_* files exist in the temp dir before the call
    fs::path tmpDir = fs::temp_directory_path();
    auto countMcpFiles = [&]() {
        int count = 0;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(tmpDir, ec)) {
            const std::string fname = e.path().filename().string();
            if (fname.rfind("mcp_script_", 0) == 0) ++count;
        }
        return count;
    };

    int before = countMcpFiles();
    adapter.ExecuteAsync(request).get();
    int after = countMcpFiles();

    EXPECT_EQ(after, before) << "Temp file(s) leaked after ExecuteAsync";
}
