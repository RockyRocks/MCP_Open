// example_plugin.cpp
// Demonstrates the MCP Native Plugin ABI.
// Exposes two tools: "ping" and "base64_encode".
//
// Build (standalone):
//   mkdir build && cd build
//   cmake .. -DMCP_HOST_INCLUDE=<path/to/host/include>
//   cmake --build .
//
// The resulting DLL/SO goes into:
//   plugins/example_plugin/bin/example_plugin.dll   (Windows)
//   plugins/example_plugin/bin/libexample_plugin.so (Linux)

#include <plugins/PluginABI.h>
#include <cstdlib>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Minimal JSON helpers (no external deps — the plugin must stand alone)
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

// Naive key lookup in a flat JSON object: {"key": "value",...}
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

// ---------------------------------------------------------------------------
// Base64 encode (RFC 4648, no padding variants)
// ---------------------------------------------------------------------------

static const char kB64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const std::string& input) {
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    for (size_t i = 0; i < input.size(); i += 3) {
        unsigned char b0 = static_cast<unsigned char>(input[i]);
        unsigned char b1 = (i + 1 < input.size())
                               ? static_cast<unsigned char>(input[i + 1]) : 0;
        unsigned char b2 = (i + 2 < input.size())
                               ? static_cast<unsigned char>(input[i + 2]) : 0;
        out += kB64Table[(b0 >> 2) & 0x3F];
        out += kB64Table[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)];
        out += (i + 1 < input.size()) ? kB64Table[((b1 & 0x0F) << 2)
                                                   | ((b2 >> 6) & 0x03)] : '=';
        out += (i + 2 < input.size()) ? kB64Table[b2 & 0x3F] : '=';
    }
    return out;
}

// ---------------------------------------------------------------------------
// Plugin state
// ---------------------------------------------------------------------------

struct ExamplePlugin {
    // No state needed for these stateless tools, but a real plugin might
    // hold configuration, connection pools, or caches here.
};

// Pre-built tool list — returned by mcp_plugin_list_tools.
// Using a static string avoids repeated heap allocation on every call.
static const char kToolList[] = R"([
  {
    "name": "ping",
    "description": "Returns 'pong' with the input echoed back. Useful for connectivity checks.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "message": {
          "type": "string",
          "description": "Optional message to echo back"
        }
      }
    }
  },
  {
    "name": "base64_encode",
    "description": "Base64-encodes an arbitrary string (RFC 4648).",
    "inputSchema": {
      "type": "object",
      "properties": {
        "input": {
          "type": "string",
          "description": "The string to encode"
        }
      },
      "required": ["input"]
    }
  }
])";

// ---------------------------------------------------------------------------
// C ABI exports
// ---------------------------------------------------------------------------

extern "C" {

MCP_PLUGIN_EXPORT uint32_t mcp_plugin_api_version() {
    return MCP_PLUGIN_API_VERSION;
}

MCP_PLUGIN_EXPORT const char* mcp_plugin_manifest() {
    return R"({"name":"example_plugin","description":"Example MCP native plugin: ping and base64_encode","version":"1.0.0"})";
}

MCP_PLUGIN_EXPORT void* mcp_plugin_create() {
    return new ExamplePlugin();
}

MCP_PLUGIN_EXPORT void mcp_plugin_destroy(void* handle) {
    delete static_cast<ExamplePlugin*>(handle);
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

    if (tool == "ping") {
        std::string msg = JsonGetString(req, "message");
        result = "{\"content\":[{\"type\":\"text\",\"text\":"
               + JsonStr("pong" + (msg.empty() ? "" : " — " + msg))
               + "}]}";

    } else if (tool == "base64_encode") {
        std::string input = JsonGetString(req, "input");
        result = "{\"content\":[{\"type\":\"text\",\"text\":"
               + JsonStr(Base64Encode(input))
               + "}]}";

    } else {
        result = "{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":"
               + JsonStr("Unknown tool: " + tool)
               + "}]}";
    }

    // Heap-allocate so the host can call mcp_plugin_free_string on it.
    char* out = static_cast<char*>(std::malloc(result.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, result.c_str(), result.size() + 1);
    return out;
}

MCP_PLUGIN_EXPORT void mcp_plugin_free_string(char* str) {
    std::free(str);
}

}  // extern "C"
