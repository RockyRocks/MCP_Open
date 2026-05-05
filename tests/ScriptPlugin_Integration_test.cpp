#include <gtest/gtest.h>
#include <plugins/ScriptPluginAdapter.h>
#include <commands/CommandRegistry.h>
#include <commands/ToolMetadata.h>
#include <plugins/ScriptPluginLoader.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

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

static const std::string kPluginsDir = SCRIPT_PLUGIN_DIR;

// ---------------------------------------------------------------------------
// git-tools plugin tests
// ---------------------------------------------------------------------------

TEST(ScriptPluginIntegration, GitTools_DiscoverTools_Returns6Tools) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "git-tools" / "scripts" / "git_tools.py").string();

    auto tools = ScriptPluginAdapter::DiscoverTools("git-tools", "python", entrypoint);
    ASSERT_EQ(tools.size(), 6u);

    std::vector<std::string> names;
    for (const auto& t : tools) names.push_back(t.m_Name);

    EXPECT_NE(std::find(names.begin(), names.end(), "git_status"),    names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "git_changes"),   names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "git_search"),    names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "git_sync"),      names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "git_reset"),     names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "git_conflicts"), names.end());
}

TEST(ScriptPluginIntegration, GitTools_StatusExecutes_ReturnsOk) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "git-tools" / "scripts" / "git_tools.py").string();

    ScriptPluginToolInfo info;
    info.m_Name = "git_status";
    ScriptPluginAdapter adapter("git-tools", "python", entrypoint, info);

    nlohmann::json request = {
        {"command", "git_status"},
        {"payload", nlohmann::json::object()}
    };
    auto result = adapter.ExecuteAsync(request).get();

    bool isError = result.value("isError", false);
    if (!isError) {
        ASSERT_TRUE(result.contains("content"));
        ASSERT_FALSE(result["content"].empty());
        std::string text = result["content"][0].value("text", "");
        EXPECT_NE(text.find("Branch:"), std::string::npos);
    }
}

TEST(ScriptPluginIntegration, GitTools_SearchMissingArgs_ReturnsError) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "git-tools" / "scripts" / "git_tools.py").string();

    ScriptPluginToolInfo info;
    info.m_Name = "git_search";
    ScriptPluginAdapter adapter("git-tools", "python", entrypoint, info);

    nlohmann::json request = {
        {"command", "git_search"},
        {"payload", nlohmann::json::object()}
    };
    auto result = adapter.ExecuteAsync(request).get();

    EXPECT_TRUE(result.value("isError", false));
}

TEST(ScriptPluginIntegration, GitTools_ConflictsNoConflicts_ReturnsEmpty) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "git-tools" / "scripts" / "git_tools.py").string();

    ScriptPluginToolInfo info;
    info.m_Name = "git_conflicts";
    ScriptPluginAdapter adapter("git-tools", "python", entrypoint, info);

    nlohmann::json request = {
        {"command", "git_conflicts"},
        {"payload", nlohmann::json::object()}
    };
    auto result = adapter.ExecuteAsync(request).get();

    EXPECT_FALSE(result.value("isError", false));
    if (result.contains("content") && !result["content"].empty()) {
        std::string text = result["content"][0].value("text", "");
        EXPECT_NE(text.find("No conflicted"), std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// github-tools plugin tests
// ---------------------------------------------------------------------------

TEST(ScriptPluginIntegration, GitHubTools_DiscoverTools_Returns9Tools) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "github-tools" / "scripts" / "github_tools.py").string();

    auto tools = ScriptPluginAdapter::DiscoverTools("github-tools", "python", entrypoint);
    ASSERT_EQ(tools.size(), 9u);

    std::vector<std::string> names;
    for (const auto& t : tools) names.push_back(t.m_Name);

    EXPECT_NE(std::find(names.begin(), names.end(), "gh_pr_status"),    names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "gh_pr_create"),    names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "gh_pr_list"),      names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "gh_pr_diff"),      names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "gh_pr_review"),    names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "gh_issue_list"),   names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "gh_issue_create"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "gh_issue_view"),   names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "gh_repo_info"),    names.end());
}

TEST(ScriptPluginIntegration, GitHubTools_PrCreateMissingTitle_ReturnsError) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "github-tools" / "scripts" / "github_tools.py").string();

    ScriptPluginToolInfo info;
    info.m_Name = "gh_pr_create";
    ScriptPluginAdapter adapter("github-tools", "python", entrypoint, info);

    nlohmann::json request = {
        {"command", "gh_pr_create"},
        {"payload", nlohmann::json::object()}
    };
    auto result = adapter.ExecuteAsync(request).get();

    EXPECT_TRUE(result.value("isError", false));
    ASSERT_TRUE(result.contains("content"));
    ASSERT_FALSE(result["content"].empty());
    std::string text = result["content"][0].value("text", "");
    EXPECT_NE(text.find("title"), std::string::npos);
}

TEST(ScriptPluginIntegration, GitHubTools_PrReviewMissingBody_ReturnsError) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "github-tools" / "scripts" / "github_tools.py").string();

    ScriptPluginToolInfo info;
    info.m_Name = "gh_pr_review";
    ScriptPluginAdapter adapter("github-tools", "python", entrypoint, info);

    nlohmann::json request = {
        {"command", "gh_pr_review"},
        {"payload", {{"pr_number", 1}, {"event", "request-changes"}}}
    };
    auto result = adapter.ExecuteAsync(request).get();

    EXPECT_TRUE(result.value("isError", false));
    ASSERT_TRUE(result.contains("content"));
    std::string text = result["content"][0].value("text", "");
    EXPECT_NE(text.find("body"), std::string::npos);
}

// ---------------------------------------------------------------------------
// github-actions plugin tests
// ---------------------------------------------------------------------------

TEST(ScriptPluginIntegration, GitHubActions_DiscoverTools_Returns4Tools) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "github-actions" / "scripts" / "github_actions.py").string();

    auto tools = ScriptPluginAdapter::DiscoverTools("github-actions", "python", entrypoint);
    ASSERT_EQ(tools.size(), 4u);

    std::vector<std::string> names;
    for (const auto& t : tools) names.push_back(t.m_Name);

    EXPECT_NE(std::find(names.begin(), names.end(), "gh_actions_status"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "gh_actions_list"),   names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "gh_actions_logs"),   names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "gh_actions_rerun"),  names.end());
}

TEST(ScriptPluginIntegration, GitHubActions_LogsMissingRunId_ReturnsError) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "github-actions" / "scripts" / "github_actions.py").string();

    ScriptPluginToolInfo info;
    info.m_Name = "gh_actions_logs";
    ScriptPluginAdapter adapter("github-actions", "python", entrypoint, info);

    nlohmann::json request = {
        {"command", "gh_actions_logs"},
        {"payload", nlohmann::json::object()}
    };
    auto result = adapter.ExecuteAsync(request).get();

    EXPECT_TRUE(result.value("isError", false));
    ASSERT_TRUE(result.contains("content"));
    std::string text = result["content"][0].value("text", "");
    EXPECT_NE(text.find("run_id"), std::string::npos);
}

// ---------------------------------------------------------------------------
// filesystem-tools plugin tests
// ---------------------------------------------------------------------------

TEST(ScriptPluginIntegration, FilesystemTools_DiscoverTools_Returns7Tools) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "filesystem-tools" / "scripts" / "filesystem_tools.py").string();

    auto tools = ScriptPluginAdapter::DiscoverTools("filesystem-tools", "python", entrypoint);
    ASSERT_EQ(tools.size(), 7u);

    std::vector<std::string> names;
    for (const auto& t : tools) names.push_back(t.m_Name);

    EXPECT_NE(std::find(names.begin(), names.end(), "fs_read"),   names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "fs_write"),  names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "fs_edit"),   names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "fs_mkdir"),  names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "fs_delete"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "fs_list"),   names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "fs_search"), names.end());
}

TEST(ScriptPluginIntegration, FilesystemTools_ReadFile_ReturnsContent) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "filesystem-tools" / "scripts" / "filesystem_tools.py").string();

    ScriptPluginToolInfo info;
    info.m_Name = "fs_read";
    ScriptPluginAdapter adapter("filesystem-tools", "python", entrypoint, info);

    std::string pluginJson = (fs::path(kPluginsDir)
        / "filesystem-tools" / "plugin.json").string();

    nlohmann::json request = {
        {"command", "fs_read"},
        {"payload", {{"path", pluginJson}}}
    };
    auto result = adapter.ExecuteAsync(request).get();

    EXPECT_FALSE(result.value("isError", false));
    ASSERT_TRUE(result.contains("content"));
    ASSERT_FALSE(result["content"].empty());
    std::string text = result["content"][0].value("text", "");
    EXPECT_NE(text.find("filesystem-tools"), std::string::npos);
}

TEST(ScriptPluginIntegration, FilesystemTools_ListDir_ReturnsEntries) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "filesystem-tools" / "scripts" / "filesystem_tools.py").string();

    ScriptPluginToolInfo info;
    info.m_Name = "fs_list";
    ScriptPluginAdapter adapter("filesystem-tools", "python", entrypoint, info);

    nlohmann::json request = {
        {"command", "fs_list"},
        {"payload", {{"path", kPluginsDir}}}
    };
    auto result = adapter.ExecuteAsync(request).get();

    EXPECT_FALSE(result.value("isError", false));
    ASSERT_TRUE(result.contains("content"));
    std::string text = result["content"][0].value("text", "");
    EXPECT_NE(text.find("filesystem-tools"), std::string::npos);
}

// ---------------------------------------------------------------------------
// shell-tools plugin tests
// ---------------------------------------------------------------------------

TEST(ScriptPluginIntegration, ShellTools_DiscoverTools_Returns4Tools) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "shell-tools" / "scripts" / "shell_tools.py").string();

    auto tools = ScriptPluginAdapter::DiscoverTools("shell-tools", "python", entrypoint);
    ASSERT_EQ(tools.size(), 4u);
}

TEST(ScriptPluginIntegration, ShellTools_ExecEcho_ReturnsOutput) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "shell-tools" / "scripts" / "shell_tools.py").string();

    ScriptPluginToolInfo info;
    info.m_Name = "shell_exec";
    ScriptPluginAdapter adapter("shell-tools", "python", entrypoint, info);

    nlohmann::json request = {
        {"command", "shell_exec"},
        {"payload", {{"command", "echo hello_mcp_test"}}}
    };
    auto result = adapter.ExecuteAsync(request).get();

    EXPECT_FALSE(result.value("isError", false));
    ASSERT_TRUE(result.contains("content"));
    std::string text = result["content"][0].value("text", "");
    EXPECT_NE(text.find("hello_mcp_test"), std::string::npos);
}

// ---------------------------------------------------------------------------
// build-tools plugin tests
// ---------------------------------------------------------------------------

TEST(ScriptPluginIntegration, BuildTools_DiscoverTools_Returns3Tools) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    std::string entrypoint = (fs::path(kPluginsDir)
        / "build-tools" / "scripts" / "build_tools.py").string();

    auto tools = ScriptPluginAdapter::DiscoverTools("build-tools", "python", entrypoint);
    ASSERT_EQ(tools.size(), 3u);

    std::vector<std::string> names;
    for (const auto& t : tools) names.push_back(t.m_Name);

    EXPECT_NE(std::find(names.begin(), names.end(), "build_run"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "test_run"),  names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "lint_run"),  names.end());
}

// ---------------------------------------------------------------------------
// ScriptPluginLoader integration — all plugins loaded together
// ---------------------------------------------------------------------------

TEST(ScriptPluginIntegration, LoadAll_RegistersAllTools) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    CommandRegistry reg;
    ScriptPluginLoader::LoadAll(kPluginsDir, reg);

    // git-tools: 6, github-tools: 9, github-actions: 4,
    // filesystem-tools: 7, shell-tools: 4, build-tools: 3 = 33 total
    auto commands = reg.ListCommands();
    EXPECT_GE(commands.size(), 33u);

    EXPECT_TRUE(reg.HasCommand("git_status"));
    EXPECT_TRUE(reg.HasCommand("gh_pr_status"));
    EXPECT_TRUE(reg.HasCommand("gh_actions_status"));
    EXPECT_TRUE(reg.HasCommand("fs_read"));
    EXPECT_TRUE(reg.HasCommand("shell_exec"));
    EXPECT_TRUE(reg.HasCommand("build_run"));
}

TEST(ScriptPluginIntegration, LoadAll_AllToolsHaveScriptPluginSource) {
    if (!IsPythonAvailable()) GTEST_SKIP() << "Python not available";

    CommandRegistry reg;
    ScriptPluginLoader::LoadAll(kPluginsDir, reg);

    for (const auto& meta : reg.ListToolMetadata()) {
        EXPECT_EQ(meta.m_Source, ToolSource::ScriptPlugin)
            << "Tool '" << meta.m_Name << "' has unexpected source";
    }
}
