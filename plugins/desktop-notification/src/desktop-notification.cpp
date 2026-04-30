// desktop-notification.cpp
// MCP Native Plugin — cross-platform desktop notifications.
//
// Exposes two MCP tools:
//   desktop_notify       — show a desktop notification
//   desktop_notify_flash — notification + taskbar/terminal flash
//
// Platform backends (selected at compile time via NotificationBackend.cpp):
//   Windows 10/11  → WinRT Toast (WRL/COM)
//   Windows older  → MessageBeep fallback
//   Linux          → notify-send subprocess
//   macOS          → osascript subprocess
//
// Build: cmake --build . --target desktop-notification
// Output: plugins/desktop-notification/bin/desktop-notification.dll|.so|.dylib

#include <plugins/PluginABI.h>
#include "NotificationBackend.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl_core.h>  // SetCurrentProcessExplicitAppUserModelID
#endif

// ============================================================================
// JSON helpers (no external deps — plugin must stand alone)
// ============================================================================

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
    while (end != std::string::npos && json[end - 1] == '\\')
        end = json.find('"', end + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

static int JsonGetInt(const std::string& json, const std::string& key, int defaultVal) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return defaultVal;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'
           || json[pos] == '\n' || json[pos] == '\r')) ++pos;
    if (pos >= json.size()) return defaultVal;
    bool neg = false;
    if (json[pos] == '-') { neg = true; ++pos; }
    if (pos >= json.size() || json[pos] < '0' || json[pos] > '9') return defaultVal;
    int val = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        val = val * 10 + (json[pos] - '0');
        ++pos;
    }
    return neg ? -val : val;
}

static NotificationType ParseType(const std::string& s) {
    if (s == "success") return NotificationType::Success;
    if (s == "warning") return NotificationType::Warning;
    if (s == "error")   return NotificationType::Error;
    return NotificationType::Info;
}

static std::string ErrorResult(const std::string& msg) {
    return "{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":"
         + JsonStr(msg) + "}]}";
}

static std::string OkResult(const std::string& msg) {
    return "{\"content\":[{\"type\":\"text\",\"text\":"
         + JsonStr(msg) + "}]}";
}

// ============================================================================
// Plugin state
// ============================================================================

struct DesktopNotificationPlugin {
    std::unique_ptr<INotificationBackend> backend;
};

// ============================================================================
// Tool schemas
// ============================================================================

static const char kToolList[] = R"JSON([
  {
    "name": "desktop_notify",
    "description": "Show a desktop notification. Windows: WinRT Toast. Linux: notify-send. macOS: Notification Center. Works with any LLM that speaks MCP.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "title": {
          "type": "string",
          "description": "Notification heading (required)"
        },
        "message": {
          "type": "string",
          "description": "Body text (required)"
        },
        "type": {
          "type": "string",
          "enum": ["info", "success", "warning", "error"],
          "description": "Visual style and audio cue. Default: info"
        },
        "duration_ms": {
          "type": "integer",
          "minimum": 1000,
          "maximum": 30000,
          "description": "Display duration in milliseconds (1000-30000, Windows/Linux only). Default: 5000"
        }
      },
      "required": ["title", "message"]
    }
  },
  {
    "name": "desktop_notify_flash",
    "description": "Show a desktop notification AND flash/beep to attract attention. Use for alerts that must not be missed.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "title": {
          "type": "string",
          "description": "Notification heading (required)"
        },
        "message": {
          "type": "string",
          "description": "Body text (required)"
        },
        "type": {
          "type": "string",
          "enum": ["info", "success", "warning", "error"],
          "description": "Visual style and audio cue. Default: info"
        },
        "duration_ms": {
          "type": "integer",
          "minimum": 1000,
          "maximum": 30000,
          "description": "Display duration in milliseconds (1000-30000, Windows/Linux only). Default: 5000"
        },
        "flash_count": {
          "type": "integer",
          "minimum": 0,
          "maximum": 20,
          "description": "Windows: taskbar flash cycles. Linux/macOS: terminal bell repetitions. Default: 3"
        }
      },
      "required": ["title", "message"]
    }
  }
])JSON";

// ============================================================================
// C ABI exports
// ============================================================================

extern "C" {

MCP_PLUGIN_EXPORT uint32_t mcp_plugin_api_version() {
    return MCP_PLUGIN_API_VERSION;
}

MCP_PLUGIN_EXPORT const char* mcp_plugin_manifest() {
    return R"({"name":"desktop-notification","description":"Cross-platform desktop notifications via WinRT Toast (Windows), notify-send (Linux), or osascript (macOS). LLM-agnostic MCP tool.","version":"1.0.0"})";
}

MCP_PLUGIN_EXPORT void* mcp_plugin_create() {
#ifdef _WIN32
    // Required for unpackaged desktop apps to route toast notifications.
    // Must be called before any WinRT toast API.
    SetCurrentProcessExplicitAppUserModelID(L"MCPServer.DesktopNotification");
#endif
    auto* plugin = new (std::nothrow) DesktopNotificationPlugin();
    if (!plugin) return nullptr;
    plugin->backend = CreateBackend();
    return plugin;
}

MCP_PLUGIN_EXPORT void mcp_plugin_destroy(void* handle) {
    delete static_cast<DesktopNotificationPlugin*>(handle);
}

MCP_PLUGIN_EXPORT const char* mcp_plugin_list_tools(void* /*handle*/) {
    return kToolList;
}

MCP_PLUGIN_EXPORT char* mcp_plugin_execute(void* handle,
                                           const char* tool_name,
                                           const char* request_json) {
    auto* plugin = static_cast<DesktopNotificationPlugin*>(handle);
    std::string tool = tool_name    ? tool_name    : "";
    std::string req  = request_json ? request_json : "{}";
    std::string result;

    if (tool == "desktop_notify" || tool == "desktop_notify_flash") {
        std::string title   = JsonGetString(req, "title");
        std::string message = JsonGetString(req, "message");

        if (title.empty() || message.empty()) {
            result = ErrorResult("'title' and 'message' are required");
        } else {
            int duration = JsonGetInt(req, "duration_ms", 5000);
            if (duration < 1000)  duration = 1000;
            if (duration > 30000) duration = 30000;

            NotificationRequest notifReq;
            notifReq.title       = title;
            notifReq.message     = message;
            notifReq.type        = ParseType(JsonGetString(req, "type"));
            notifReq.duration_ms = duration;
            notifReq.flash_count = 0;

            bool ok = plugin->backend->Send(notifReq);

            if (tool == "desktop_notify_flash") {
                int flash = JsonGetInt(req, "flash_count", 3);
                if (flash < 0)  flash = 0;
                if (flash > 20) flash = 20;
                if (flash > 0)
                    plugin->backend->Flash(flash);
            }

            if (ok) {
                std::string backend = plugin->backend->GetBackendName();
                result = OkResult("Notification sent via " + backend + " backend");
            } else {
                result = ErrorResult("Notification backend returned failure");
            }
        }
    } else {
        result = ErrorResult("Unknown tool: " + tool);
    }

    char* out = static_cast<char*>(std::malloc(result.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, result.c_str(), result.size() + 1);
    return out;
}

MCP_PLUGIN_EXPORT void mcp_plugin_free_string(char* str) {
    std::free(str);
}

}  // extern "C"
