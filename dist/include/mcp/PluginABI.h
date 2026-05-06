#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// MCP Native Plugin ABI
//
// This is the ONLY header plugin authors need to include. It defines a stable
// extern "C" interface that works regardless of which compiler, compiler
// version, or C++ standard library the plugin was built with.
//
// Version encoding:  0xMMMMmmmm  (upper 16 = major, lower 16 = minor)
//   - Host refuses to load if major version differs (breaking ABI change).
//   - Host loads with a warning if plugin minor > host minor (newer optional
//     feature the host doesn't know about yet).
//   - Host loads silently if plugin minor <= host minor (backward compatible).
// ---------------------------------------------------------------------------

#define MCP_PLUGIN_API_VERSION  UINT32_C(0x00010000)  // major=1, minor=0

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Function pointer typedefs — used by the host to call into the plugin.
// ---------------------------------------------------------------------------

/// Returns the API version this plugin was compiled against.
typedef uint32_t (*mcp_api_version_fn)();

/// Returns a null-terminated UTF-8 JSON string describing the plugin:
///   { "name": "...", "description": "...", "version": "..." }
/// The returned pointer is owned by the plugin and must NOT be freed by
/// the caller. It remains valid for the lifetime of the plugin handle.
typedef const char* (*mcp_manifest_fn)();

/// Creates and returns an opaque plugin handle. Called once after the
/// version check. Returns NULL to signal a fatal initialisation failure.
typedef void* (*mcp_create_fn)();

/// Destroys the handle returned by mcp_plugin_create. The host always
/// calls this before unloading the library.
typedef void (*mcp_destroy_fn)(void* handle);

/// Returns a null-terminated JSON array of tool descriptors visible to the
/// host:
///   [ { "name":"...", "description":"...", "inputSchema": {...} }, ... ]
/// The pointer is owned by the plugin; do NOT free it. It must remain
/// valid until the next call to mcp_list_tools or mcp_plugin_destroy.
typedef const char* (*mcp_list_tools_fn)(void* handle);

/// Executes a named tool.
///   handle       — opaque handle from mcp_plugin_create
///   tool_name    — null-terminated tool name (from mcp_list_tools)
///   request_json — null-terminated JSON object with the tool's arguments
///
/// Returns a newly heap-allocated null-terminated JSON result string.
/// The caller MUST free the memory via mcp_free_string_fn.
/// On unrecoverable error the plugin may return:
///   { "isError": true, "content": [{"type":"text","text":"..."}] }
/// The plugin MUST NOT throw a C++ exception across this boundary.
typedef char* (*mcp_execute_fn)(void* handle, const char* tool_name,
                                const char* request_json);

/// Frees a string previously returned by mcp_execute_fn.
/// The host always calls this after consuming the result.
typedef void (*mcp_free_string_fn)(char* str);

// ---------------------------------------------------------------------------
// Canonical export symbol names (used by dlsym / GetProcAddress).
// ---------------------------------------------------------------------------
#define MCP_SYM_API_VERSION  "mcp_plugin_api_version"
#define MCP_SYM_MANIFEST     "mcp_plugin_manifest"
#define MCP_SYM_CREATE       "mcp_plugin_create"
#define MCP_SYM_DESTROY      "mcp_plugin_destroy"
#define MCP_SYM_LIST_TOOLS   "mcp_plugin_list_tools"
#define MCP_SYM_EXECUTE      "mcp_plugin_execute"
#define MCP_SYM_FREE_STRING  "mcp_plugin_free_string"

// ---------------------------------------------------------------------------
// Convenience macro for plugin implementations.
// Usage inside a plugin .cpp:
//   MCP_PLUGIN_EXPORT uint32_t mcp_plugin_api_version() { ... }
// ---------------------------------------------------------------------------
#ifdef _WIN32
#define MCP_PLUGIN_EXPORT __declspec(dllexport)
#else
#define MCP_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
}  // extern "C"
#endif
