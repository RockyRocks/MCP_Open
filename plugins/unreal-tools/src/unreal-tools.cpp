// unreal-tools.cpp
// MCP Native Plugin — Unreal Engine development tools.
// Reference implementation for game engine native plugins.
//
// Exposes six tools:
//   ue_project_info   — parse .uproject for metadata
//   ue_build           — invoke RunUAT / UBT
//   ue_run_tests       — run UE automation tests
//   ue_asset_search    — search Content/ for assets
//   ue_log_parser      — parse UE log files
//   ue_module_info     — list Source/ modules and targets
//
// Build (standalone):
//   mkdir build && cd build
//   cmake .. -DMCP_HOST_INCLUDE=<path/to/host/include>
//   cmake --build .

#include <plugins/PluginABI.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <regex>

#if __cplusplus >= 201703L
#include <filesystem>
namespace fs = std::filesystem;
#else
// Fallback: disable filesystem-dependent tools at compile time
#define NO_FILESYSTEM
#endif

// ---------------------------------------------------------------------------
// Minimal JSON helpers (no external deps — plugin must stand alone)
// ---------------------------------------------------------------------------

static std::string JsonStr(const std::string& v) {
    std::string out = "\"";
    for (char c : v) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    out += '"';
    return out;
}

static std::string JsonGetString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    auto end = json.find('"', pos + 1);
    while (end != std::string::npos && json[end - 1] == '\\') {
        end = json.find('"', end + 1);
    }
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

static bool JsonGetBool(const std::string& json, const std::string& key, bool defaultVal = false) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return defaultVal;
    auto rest = json.substr(pos + 1);
    size_t start = rest.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return defaultVal;
    return rest.substr(start, 4) == "true";
}

static int JsonGetInt(const std::string& json, const std::string& key, int defaultVal = 0) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return defaultVal;
    auto rest = json.substr(pos + 1);
    size_t start = rest.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return defaultVal;
    try { return std::stoi(rest.substr(start)); }
    catch (...) { return defaultVal; }
}

static std::string JsonGetArray(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "[]";
    pos = json.find('[', pos + needle.size());
    if (pos == std::string::npos) return "[]";
    int depth = 0;
    size_t end = pos;
    for (; end < json.size(); ++end) {
        if (json[end] == '[') ++depth;
        else if (json[end] == ']') { --depth; if (depth == 0) { ++end; break; } }
    }
    return json.substr(pos, end - pos);
}

static std::string ReadFile(const std::string& path, size_t maxBytes = 500000) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    char buf[8192];
    size_t total = 0;
    while (f && total < maxBytes) {
        f.read(buf, sizeof(buf));
        auto n = f.gcount();
        if (n > 0) { ss.write(buf, n); total += static_cast<size_t>(n); }
    }
    return ss.str();
}

static std::string MakeOk(const std::string& text) {
    return "{\"content\":[{\"type\":\"text\",\"text\":" + JsonStr(text) + "}]}";
}

static std::string MakeError(const std::string& text) {
    return "{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":"
         + JsonStr(text) + "}]}";
}

static char* HeapCopy(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

static std::string RunCommand(const std::string& cmd) {
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return "[Failed to run command]";
    std::string output;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe)) {
        output += buf;
        if (output.size() > 500000) break;
    }
#ifdef _WIN32
    int rc = _pclose(pipe);
#else
    int rc = pclose(pipe);
#endif
    output += "\nExit code: " + std::to_string(rc);
    return output;
}

// ---------------------------------------------------------------------------
// Plugin state
// ---------------------------------------------------------------------------

struct UnrealPlugin {};

// ---------------------------------------------------------------------------
// Tool list
// ---------------------------------------------------------------------------

static const char kToolList[] = R"JSON([
  {
    "name": "ue_project_info",
    "description": "Parse an Unreal Engine .uproject file and return project metadata: engine version, modules, enabled plugins, target platforms, and description.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "project_file": {
          "type": "string",
          "description": "Path to the .uproject file"
        }
      },
      "required": ["project_file"]
    }
  },
  {
    "name": "ue_build",
    "description": "Invoke Unreal Build Tool (UBT) or RunUAT to build an Unreal project. Supports target, configuration, and platform selection.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "project_file": {
          "type": "string",
          "description": "Path to the .uproject file"
        },
        "uat_path": {
          "type": "string",
          "description": "Path to RunUAT.bat (Win) or RunUAT.sh (Linux/Mac)"
        },
        "target": {
          "type": "string",
          "enum": ["Editor", "Game", "Client", "Server"],
          "description": "Build target (default: Editor)"
        },
        "config": {
          "type": "string",
          "enum": ["Development", "Shipping", "Debug", "DebugGame", "Test"],
          "description": "Build configuration (default: Development)"
        },
        "platform": {
          "type": "string",
          "enum": ["Win64", "Linux", "Mac", "Android", "iOS"],
          "description": "Target platform (default: Win64)"
        },
        "clean": {
          "type": "boolean",
          "description": "Clean before building (default: false)"
        }
      },
      "required": ["project_file", "uat_path"]
    }
  },
  {
    "name": "ue_run_tests",
    "description": "Run Unreal Engine automation tests and parse the results. Uses the editor's -ExecCmds for headless test execution.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "project_file": {
          "type": "string",
          "description": "Path to the .uproject file"
        },
        "editor_path": {
          "type": "string",
          "description": "Path to UnrealEditor executable"
        },
        "test_filter": {
          "type": "string",
          "description": "Test name filter (e.g. 'Project.Functional', 'Navigation')"
        },
        "report_path": {
          "type": "string",
          "description": "Path to write JSON test report (default: Saved/Automation/)"
        }
      },
      "required": ["project_file", "editor_path"]
    }
  },
  {
    "name": "ue_asset_search",
    "description": "Search the Content/ directory tree for .uasset, .umap, and other Unreal asset files by name pattern and extension.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "project_dir": {
          "type": "string",
          "description": "Path to the UE project root directory"
        },
        "query": {
          "type": "string",
          "description": "File name pattern (glob, e.g. '*Character*', 'BP_*')"
        },
        "extension": {
          "type": "string",
          "description": "Filter by extension (e.g. .uasset, .umap, .uobject)"
        },
        "content_subdir": {
          "type": "string",
          "description": "Subdirectory under Content/ to search"
        },
        "limit": {
          "type": "integer",
          "description": "Maximum results (default: 100)"
        }
      },
      "required": ["project_dir"]
    }
  },
  {
    "name": "ue_log_parser",
    "description": "Parse Unreal Engine log files for errors, warnings, and fatal messages. Supports filtering by severity and log category.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "log_path": {
          "type": "string",
          "description": "Path to the UE log file (e.g. Saved/Logs/MyProject.log)"
        },
        "severity": {
          "type": "string",
          "enum": ["Error", "Warning", "Fatal", "All"],
          "description": "Filter by severity (default: Error)"
        },
        "category": {
          "type": "string",
          "description": "Filter by log category (e.g. LogTemp, LogBlueprintUserMessages)"
        },
        "limit": {
          "type": "integer",
          "description": "Maximum entries (default: 50)"
        }
      },
      "required": ["log_path"]
    }
  },
  {
    "name": "ue_module_info",
    "description": "Scan Source/ for .Build.cs and .Target.cs files to list all modules, their dependencies, and target configurations.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "project_dir": {
          "type": "string",
          "description": "Path to the UE project root directory"
        }
      },
      "required": ["project_dir"]
    }
  }
])JSON";

// ---------------------------------------------------------------------------
// Tool implementations
// ---------------------------------------------------------------------------

static std::string HandleProjectInfo(const std::string& req) {
    std::string projectFile = JsonGetString(req, "project_file");
    if (projectFile.empty()) return MakeError("project_file is required");

    std::string content = ReadFile(projectFile, 200000);
    if (content.empty()) return MakeError("Cannot read .uproject file: " + projectFile);

    std::string engineVer = JsonGetString(content, "EngineAssociation");
    std::string description = JsonGetString(content, "Description");
    std::string category = JsonGetString(content, "Category");

    std::string modulesArr = JsonGetArray(content, "Modules");
    std::string pluginsArr = JsonGetArray(content, "Plugins");

    std::ostringstream out;
    out << "Unreal Project: " << projectFile << "\n";
    out << "  Engine: " << (engineVer.empty() ? "unknown" : engineVer) << "\n";
    if (!description.empty()) out << "  Description: " << description << "\n";
    if (!category.empty()) out << "  Category: " << category << "\n";
    out << "  Modules: " << modulesArr << "\n";
    out << "  Plugins: " << pluginsArr << "\n";

    return MakeOk(out.str());
}

static std::string HandleBuild(const std::string& req) {
    std::string projectFile = JsonGetString(req, "project_file");
    std::string uatPath = JsonGetString(req, "uat_path");
    if (projectFile.empty()) return MakeError("project_file is required");
    if (uatPath.empty()) return MakeError("uat_path is required");

    std::string target = JsonGetString(req, "target");
    std::string config = JsonGetString(req, "config");
    std::string plat = JsonGetString(req, "platform");
    bool clean = JsonGetBool(req, "clean");

    if (target.empty()) target = "Editor";
    if (config.empty()) config = "Development";
    if (plat.empty()) plat = "Win64";

    std::ostringstream cmd;
    cmd << "\"" << uatPath << "\" BuildCookRun"
        << " -project=\"" << projectFile << "\""
        << " -targetplatform=" << plat
        << " -clientconfig=" << config
        << " -build";

    if (target == "Editor") cmd << " -nocompileEditor";
    if (clean) cmd << " -clean";
    cmd << " -utf8output 2>&1";

    std::string output = RunCommand(cmd.str());

    std::ostringstream result;
    result << "Build: " << target << " | " << config << " | " << plat << "\n";
    result << "Command: " << cmd.str() << "\n";
    result << "--- output (last 5000 chars) ---\n"
           << output.substr(output.size() > 5000 ? output.size() - 5000 : 0);

    return MakeOk(result.str());
}

static std::string HandleRunTests(const std::string& req) {
    std::string projectFile = JsonGetString(req, "project_file");
    std::string editorPath = JsonGetString(req, "editor_path");
    if (projectFile.empty()) return MakeError("project_file is required");
    if (editorPath.empty()) return MakeError("editor_path is required");

    std::string testFilter = JsonGetString(req, "test_filter");
    std::string reportPath = JsonGetString(req, "report_path");

    if (testFilter.empty()) testFilter = "Project";

    std::ostringstream cmd;
    cmd << "\"" << editorPath << "\""
        << " \"" << projectFile << "\""
        << " -ExecCmds=\"Automation RunTests " << testFilter << ";Quit\""
        << " -Unattended -NullRHI -NoSound -NoSplash"
        << " -log 2>&1";

    std::string output = RunCommand(cmd.str());

    std::ostringstream result;
    result << "Test filter: " << testFilter << "\n";
    result << "Command: " << cmd.str() << "\n";

    size_t passCount = 0, failCount = 0;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("Success") != std::string::npos) ++passCount;
        if (line.find("Fail") != std::string::npos) ++failCount;
    }
    result << "Summary: ~" << passCount << " passed, ~" << failCount << " failed\n";
    result << "--- output (last 5000 chars) ---\n"
           << output.substr(output.size() > 5000 ? output.size() - 5000 : 0);

    return MakeOk(result.str());
}

static std::string HandleAssetSearch(const std::string& req) {
#ifdef NO_FILESYSTEM
    (void)req;
    return MakeError("Asset search requires C++17 <filesystem> support");
#else
    std::string projectDir = JsonGetString(req, "project_dir");
    if (projectDir.empty()) return MakeError("project_dir is required");

    std::string query = JsonGetString(req, "query");
    std::string extension = JsonGetString(req, "extension");
    std::string subdir = JsonGetString(req, "content_subdir");
    int limit = JsonGetInt(req, "limit", 100);
    if (limit <= 0) limit = 100;

    if (query.empty()) query = "*";

    fs::path contentPath = fs::path(projectDir) / "Content";
    if (!subdir.empty()) {
        if (subdir.find("..") != std::string::npos)
            return MakeError("Path traversal not allowed");
        contentPath /= subdir;
    }

    if (!fs::is_directory(contentPath))
        return MakeError("Content directory not found: " + contentPath.string());

    if (!extension.empty() && extension[0] != '.') extension = "." + extension;

    std::vector<std::string> matches;
    std::error_code ec;
    for (auto& entry : fs::recursive_directory_iterator(contentPath, ec)) {
        if (!entry.is_regular_file()) continue;

        auto fname = entry.path().filename().string();
        if (!extension.empty()) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            std::string extLower = extension;
            std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
            if (ext != extLower) continue;
        }

        if (query != "*") {
            std::string fnameLower = fname;
            std::transform(fnameLower.begin(), fnameLower.end(), fnameLower.begin(), ::tolower);
            std::string queryLower = query;
            std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);
            // Simple wildcard: *X* → contains X
            std::string pattern = queryLower;
            if (pattern.front() == '*' && pattern.back() == '*' && pattern.size() > 2) {
                std::string sub = pattern.substr(1, pattern.size() - 2);
                if (fnameLower.find(sub) == std::string::npos) continue;
            } else if (pattern.front() == '*') {
                std::string suffix = pattern.substr(1);
                if (fnameLower.size() < suffix.size() ||
                    fnameLower.compare(fnameLower.size() - suffix.size(), suffix.size(), suffix) != 0)
                    continue;
            } else if (pattern.back() == '*') {
                std::string prefix = pattern.substr(0, pattern.size() - 1);
                if (fnameLower.compare(0, prefix.size(), prefix) != 0) continue;
            } else {
                if (fnameLower != queryLower) continue;
            }
        }

        auto rel = fs::relative(entry.path(), projectDir, ec);
        auto size = entry.file_size(ec);
        std::string sizeStr;
        if (size > 1048576) sizeStr = std::to_string(size / 1048576) + " MB";
        else if (size > 1024) sizeStr = std::to_string(size / 1024) + " KB";
        else sizeStr = std::to_string(size) + " B";

        matches.push_back(rel.generic_string() + "  (" + sizeStr + ")");
        if (static_cast<int>(matches.size()) >= limit) break;
    }

    std::ostringstream out;
    out << "Found " << matches.size() << " assets (query='" << query
        << "' ext='" << extension << "')\n";
    for (auto& m : matches) out << "  " << m << "\n";

    return MakeOk(out.str());
#endif
}

static std::string HandleLogParser(const std::string& req) {
    std::string logPath = JsonGetString(req, "log_path");
    if (logPath.empty()) return MakeError("log_path is required");

    std::string severity = JsonGetString(req, "severity");
    std::string category = JsonGetString(req, "category");
    int limit = JsonGetInt(req, "limit", 50);
    if (limit <= 0) limit = 50;

    if (severity.empty()) severity = "Error";

    std::string content = ReadFile(logPath, 2000000);
    if (content.empty()) return MakeError("Cannot read log file: " + logPath);

    // UE log format: [YYYY.MM.DD-HH.MM.SS:mmm][idx]LogCategory: Level: message
    std::regex logPattern(R"(\[\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3}\]\[\s*\d+\](\w+):\s*(Display|Verbose|VeryVerbose|Log|Warning|Error|Fatal):\s*(.*))");

    std::vector<std::string> entries;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line) && static_cast<int>(entries.size()) < limit) {
        std::smatch match;
        if (!std::regex_search(line, match, logPattern)) continue;

        std::string cat = match[1].str();
        std::string sev = match[2].str();
        std::string msg = match[3].str();

        if (!category.empty() && cat != category) continue;

        bool include = false;
        if (severity == "All") include = (sev == "Warning" || sev == "Error" || sev == "Fatal");
        else if (severity == "Error") include = (sev == "Error" || sev == "Fatal");
        else if (severity == "Warning") include = (sev == "Warning");
        else if (severity == "Fatal") include = (sev == "Fatal");

        if (!include) continue;

        std::string entry = "[" + sev + "] " + cat + ": " + msg;
        if (entry.size() > 300) entry = entry.substr(0, 300) + "...";
        entries.push_back(entry);
    }

    if (entries.empty())
        return MakeOk("No " + severity + " entries found in " + logPath);

    std::ostringstream out;
    out << "Log: " << logPath << "  (" << entries.size() << " entries, severity=" << severity << ")\n";
    for (auto& e : entries) out << "  " << e << "\n";

    return MakeOk(out.str());
}

static std::string HandleModuleInfo(const std::string& req) {
#ifdef NO_FILESYSTEM
    (void)req;
    return MakeError("Module info requires C++17 <filesystem> support");
#else
    std::string projectDir = JsonGetString(req, "project_dir");
    if (projectDir.empty()) return MakeError("project_dir is required");

    fs::path sourcePath = fs::path(projectDir) / "Source";
    if (!fs::is_directory(sourcePath))
        return MakeError("Source/ directory not found: " + sourcePath.string());

    std::vector<std::string> buildFiles;
    std::vector<std::string> targetFiles;
    std::error_code ec;

    for (auto& entry : fs::recursive_directory_iterator(sourcePath, ec)) {
        if (!entry.is_regular_file()) continue;
        auto fname = entry.path().filename().string();
        if (fname.size() > 9 && fname.substr(fname.size() - 9) == ".Build.cs")
            buildFiles.push_back(entry.path().string());
        else if (fname.size() > 10 && fname.substr(fname.size() - 10) == ".Target.cs")
            targetFiles.push_back(entry.path().string());
    }

    std::ostringstream out;
    out << "Modules (" << buildFiles.size() << " .Build.cs files):\n";
    for (auto& bf : buildFiles) {
        auto rel = fs::relative(fs::path(bf), projectDir, ec);
        auto moduleName = fs::path(bf).stem().stem().string(); // strip .Build.cs

        std::string content = ReadFile(bf, 50000);

        // Extract dependencies
        std::vector<std::string> deps;
        std::regex depPattern(R"(\"(\w+)\")");
        auto depSection = content.find("PublicDependencyModuleNames");
        if (depSection != std::string::npos) {
            auto end = content.find(';', depSection);
            std::string sub = content.substr(depSection, end - depSection);
            auto it = std::sregex_iterator(sub.begin(), sub.end(), depPattern);
            auto itEnd = std::sregex_iterator();
            for (; it != itEnd; ++it) deps.push_back((*it)[1].str());
        }

        out << "  " << moduleName << " (" << rel.generic_string() << ")\n";
        if (!deps.empty()) {
            out << "    Dependencies: ";
            for (size_t i = 0; i < deps.size(); ++i) {
                if (i > 0) out << ", ";
                out << deps[i];
            }
            out << "\n";
        }
    }

    out << "\nTargets (" << targetFiles.size() << " .Target.cs files):\n";
    for (auto& tf : targetFiles) {
        auto rel = fs::relative(fs::path(tf), projectDir, ec);
        auto targetName = fs::path(tf).stem().stem().string();

        std::string content = ReadFile(tf, 50000);

        std::string targetType;
        std::regex typePattern(R"(Type\s*=\s*TargetType\.(\w+))");
        std::smatch tm;
        if (std::regex_search(content, tm, typePattern))
            targetType = tm[1].str();

        out << "  " << targetName;
        if (!targetType.empty()) out << " (Type: " << targetType << ")";
        out << " — " << rel.generic_string() << "\n";
    }

    return MakeOk(out.str());
#endif
}

// ---------------------------------------------------------------------------
// C ABI exports
// ---------------------------------------------------------------------------

extern "C" {

MCP_PLUGIN_EXPORT uint32_t mcp_plugin_api_version() {
    return MCP_PLUGIN_API_VERSION;
}

MCP_PLUGIN_EXPORT const char* mcp_plugin_manifest() {
    return R"({"name":"unreal-tools","description":"Unreal Engine development tools: project info, build, test, asset search, log parsing, module info","version":"1.0.0"})";
}

MCP_PLUGIN_EXPORT void* mcp_plugin_create() {
    return new UnrealPlugin();
}

MCP_PLUGIN_EXPORT void mcp_plugin_destroy(void* handle) {
    delete static_cast<UnrealPlugin*>(handle);
}

MCP_PLUGIN_EXPORT const char* mcp_plugin_list_tools(void* /*handle*/) {
    return kToolList;
}

MCP_PLUGIN_EXPORT char* mcp_plugin_execute(void* /*handle*/,
                                           const char* tool_name,
                                           const char* request_json) {
    std::string tool = tool_name ? tool_name : "";
    std::string req  = request_json ? request_json : "{}";
    std::string result;

    if      (tool == "ue_project_info") result = HandleProjectInfo(req);
    else if (tool == "ue_build")        result = HandleBuild(req);
    else if (tool == "ue_run_tests")    result = HandleRunTests(req);
    else if (tool == "ue_asset_search") result = HandleAssetSearch(req);
    else if (tool == "ue_log_parser")   result = HandleLogParser(req);
    else if (tool == "ue_module_info")  result = HandleModuleInfo(req);
    else {
        result = MakeError("Unknown tool: " + tool);
    }

    return HeapCopy(result);
}

MCP_PLUGIN_EXPORT void mcp_plugin_free_string(char* str) {
    std::free(str);
}

}  // extern "C"
