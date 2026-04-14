#include <plugins/ScriptPluginAdapter.h>
#include <commands/ToolMetadata.h>
#include <core/Logger.h>

#include <atomic>
#include <cstdio>       // popen/_popen, fgets, pclose/_pclose
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <process.h>    // _getpid
#else
#include <unistd.h>     // getpid
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// RAII guard that removes a temp file on destruction (never throws).
///
/// Rule of Five: user-declared destructor → must explicitly handle all four
/// remaining specials.  This guard is always a stack-local; copying would
/// cause a double-remove and moving is unnecessary, so both are deleted.
/// The explicit single-argument constructor makes initialization unambiguous
/// and satisfies MSVC's stricter aggregate rules (which don't recognise
/// deleted constructors as "non-user-provided" in all configurations).
struct TempFileGuard {
    fs::path path;

    explicit TempFileGuard(fs::path p) noexcept : path(std::move(p)) {}
    ~TempFileGuard() {
        std::error_code ec;
        fs::remove(path, ec);  // error_code overload: never throws
    }
    TempFileGuard(const TempFileGuard&)            = delete;
    TempFileGuard& operator=(const TempFileGuard&) = delete;
    TempFileGuard(TempFileGuard&&)                 = delete;
    TempFileGuard& operator=(TempFileGuard&&)      = delete;
};

/// Build a canonical JSON error response matching NativePluginAdapter's format.
nlohmann::json ErrorResponse(const std::string& msg) {
    return {
        {"isError", true},
        {"content", {{{"type", "text"}, {"text", msg}}}}
    };
}

/// Run a command via popen, read stdout up to maxBytes, return output string.
/// Returns empty string on pipe open failure; logs the error.
std::string RunCommand(const std::string& cmd, size_t maxBytes) {
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) {
        Logger::GetInstance().Log("[ScriptPlugin] Failed to spawn: " + cmd);
        return "";
    }

    std::string output;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
        if (output.size() >= maxBytes) {
            output.resize(maxBytes);
            Logger::GetInstance().Log(
                "[ScriptPlugin] Output truncated at " + std::to_string(maxBytes) + " bytes");
            break;
        }
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return output;
}

/// Strip trailing whitespace and newline characters from str in-place.
void TrimRight(std::string& str) {
    while (!str.empty() && (str.back() == '\n' || str.back() == '\r'
                             || str.back() == ' ' || str.back() == '\t'))
        str.pop_back();
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// ScriptPluginAdapter — static helpers
// ---------------------------------------------------------------------------

std::string ScriptPluginAdapter::GetRuntimeExecutable(const std::string& runtime) {
    if (runtime == "python") {
#ifdef _WIN32
        return "python";
#else
        return "python3";
#endif
    }
    if (runtime == "node")   return "node";
    if (runtime == "dotnet") return "dotnet";
    // "executable" → caller handles it (no exe prefix)
    // anything else → use as-is (e.g. "python3", "node20")
    return runtime;
}

bool ScriptPluginAdapter::IsValidToolName(const std::string& name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

std::string ScriptPluginAdapter::BuildListCommand(const std::string& runtime,
                                                   const std::string& entrypoint) {
    if (runtime == "executable")
        return "\"" + entrypoint + "\" --mcp-list";
    return GetRuntimeExecutable(runtime) + " \"" + entrypoint + "\" --mcp-list";
}

std::string ScriptPluginAdapter::BuildCallCommand(const std::string& runtime,
                                                   const std::string& entrypoint,
                                                   const std::string& toolName,
                                                   const std::string& argsFilePath) {
    // toolName has already been validated — safe to embed unquoted
    std::string prefix = (runtime == "executable")
        ? "\"" + entrypoint + "\""
        : GetRuntimeExecutable(runtime) + " \"" + entrypoint + "\"";
    return prefix + " --mcp-call " + toolName
           + " --mcp-args-file \"" + argsFilePath + "\"";
}

// ---------------------------------------------------------------------------
// ScriptPluginAdapter — DiscoverTools
// ---------------------------------------------------------------------------

std::vector<ScriptPluginToolInfo> ScriptPluginAdapter::DiscoverTools(
    const std::string& pluginName,
    const std::string& runtime,
    const std::string& entrypoint)
{
    std::vector<ScriptPluginToolInfo> result;
    try {
        std::string cmd = BuildListCommand(runtime, entrypoint);
        std::string output = RunCommand(cmd, kMaxOutputBytes);
        if (output.empty()) {
            Logger::GetInstance().Log(
                "[ScriptPlugin] " + pluginName + ": --mcp-list produced no output");
            return {};
        }

        TrimRight(output);
        auto arr = nlohmann::json::parse(output);
        if (!arr.is_array()) {
            Logger::GetInstance().Log(
                "[ScriptPlugin] " + pluginName + ": --mcp-list output is not a JSON array");
            return {};
        }

        for (const auto& item : arr) {
            if (!item.contains("name") || !item["name"].is_string()) continue;
            std::string name = item["name"].get<std::string>();
            if (!IsValidToolName(name)) {
                Logger::GetInstance().Log(
                    "[ScriptPlugin] " + pluginName
                    + ": skipping tool with invalid name \"" + name + "\"");
                continue;
            }
            ScriptPluginToolInfo info;
            info.m_Name        = name;
            info.m_Description = item.value("description", "");
            if (item.contains("inputSchema") && item["inputSchema"].is_object())
                info.m_InputSchema = item["inputSchema"];
            result.push_back(std::move(info));
        }
    } catch (const std::exception& e) {
        Logger::GetInstance().Log(
            "[ScriptPlugin] " + pluginName + ": DiscoverTools failed: " + e.what());
        return {};
    }
    return result;
}

// ---------------------------------------------------------------------------
// ScriptPluginAdapter — constructor, GetMetadata, ExecuteAsync
// ---------------------------------------------------------------------------

ScriptPluginAdapter::ScriptPluginAdapter(std::string pluginName,
                                          std::string runtime,
                                          std::string entrypoint,
                                          ScriptPluginToolInfo toolInfo)
    : m_PluginName(std::move(pluginName))
    , m_Runtime(std::move(runtime))
    , m_Entrypoint(std::move(entrypoint))
    , m_ToolInfo(std::move(toolInfo))
{}

ToolMetadata ScriptPluginAdapter::GetMetadata() const {
    ToolMetadata meta;
    meta.m_Name        = m_ToolInfo.m_Name;
    meta.m_Description = m_ToolInfo.m_Description;
    meta.m_InputSchema = (m_ToolInfo.m_InputSchema.is_null() || m_ToolInfo.m_InputSchema.empty())
        ? nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}}
        : m_ToolInfo.m_InputSchema;
    meta.m_Source      = ToolSource::ScriptPlugin;
    meta.m_Hidden      = false;
    return meta;
}

std::future<nlohmann::json> ScriptPluginAdapter::ExecuteAsync(const nlohmann::json& request) {
    auto runtime    = m_Runtime;
    auto entrypoint = m_Entrypoint;
    auto toolName   = m_ToolInfo.m_Name;

    return std::async(std::launch::async, [runtime, entrypoint, toolName, request]()
        -> nlohmann::json
    {
        // 1. Extract arguments from request
        nlohmann::json args = request.value("payload",
                              request.value("arguments", nlohmann::json::object()));

        // 2. Generate a unique temp file path
        static std::atomic<uint64_t> s_Counter{0};
        uint64_t n = s_Counter.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
        int pid = _getpid();
#else
        int pid = getpid();
#endif
        fs::path tmpPath = fs::temp_directory_path()
            / ("mcp_script_" + std::to_string(n) + "_" + std::to_string(pid) + ".json");

        // 3. RAII guard — removes tmpPath on scope exit even if an exception fires
        TempFileGuard guard{tmpPath};

        // 4. Write args to temp file
        {
            std::ofstream f(tmpPath);
            if (!f.is_open())
                return ErrorResponse("Failed to create temp args file: " + tmpPath.string());
            f << args.dump();
        }

        // 5. Build command and run
        std::string cmd = BuildCallCommand(runtime, entrypoint, toolName, tmpPath.string());
        std::string output = RunCommand(cmd, kMaxOutputBytes);

        // 6. TempFileGuard destructor fires here (tmpPath removed)

        // 7. Parse result
        if (output.empty())
            return ErrorResponse("Script returned no output for tool: " + toolName);

        TrimRight(output);

        try {
            auto result = nlohmann::json::parse(output);

            if (result.value("status", "") == "error") {
                return {
                    {"isError", true},
                    {"content", {{{"type", "text"},
                                  {"text", result.value("error", "Script reported error")}}}}
                };
            }

            // Wrap plain string content as MCP content array
            if (result.contains("content") && result["content"].is_string()) {
                nlohmann::json wrapped = result;
                wrapped["content"] = {{{"type", "text"},
                                        {"text", result["content"].get<std::string>()}}};
                return wrapped;
            }

            return result;

        } catch (const nlohmann::json::parse_error& e) {
            return ErrorResponse(
                std::string("Script output is not valid JSON: ") + e.what()
                + " | output: " + output.substr(0, 200));
        }
    });
}
