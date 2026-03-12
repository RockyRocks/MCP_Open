#ifndef MCP_CAPI_H
#define MCP_CAPI_H

#ifdef _WIN32
    #ifdef MCP_CAPI_EXPORTS
        #define MCP_API __declspec(dllexport)
    #else
        #define MCP_API __declspec(dllimport)
    #endif
#else
    #define MCP_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* McpServerHandle;

/* Lifecycle */
MCP_API McpServerHandle mcp_server_create(const char* configPath);
MCP_API void            mcp_server_destroy(McpServerHandle handle);
MCP_API int             mcp_server_start(McpServerHandle handle, int port);
MCP_API void            mcp_server_stop(McpServerHandle handle);

/* Command Registry */
MCP_API int mcp_command_list(McpServerHandle handle,
                             char* buffer, int bufferSize);
MCP_API int mcp_command_has(McpServerHandle handle, const char* name);

/* Request handling */
MCP_API int mcp_handle_request(McpServerHandle handle,
                                const char* jsonBody,
                                const char* clientIp,
                                char* responseBuffer,
                                int bufferSize);

/* LLM */
MCP_API int mcp_llm_complete(McpServerHandle handle,
                              const char* requestJson,
                              char* responseBuffer,
                              int bufferSize);

/* Skills */
MCP_API int mcp_skill_list(McpServerHandle handle,
                            char* buffer, int bufferSize);
MCP_API int mcp_skill_execute(McpServerHandle handle,
                               const char* requestJson,
                               char* responseBuffer,
                               int bufferSize);

/* Servers */
MCP_API int mcp_server_list_remote(McpServerHandle handle,
                                    char* buffer, int bufferSize);

/* Error handling */
MCP_API const char* mcp_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* MCP_CAPI_H */
