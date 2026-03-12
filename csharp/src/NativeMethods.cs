using System;
using System.Runtime.InteropServices;

namespace McpClient
{
    internal static class NativeMethods
    {
        private const string LibName = "mcp_capi";

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr mcp_server_create(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? configPath);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void mcp_server_destroy(IntPtr handle);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mcp_server_start(IntPtr handle, int port);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void mcp_server_stop(IntPtr handle);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mcp_command_list(
            IntPtr handle, byte[] buffer, int bufferSize);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mcp_command_has(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mcp_handle_request(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonBody,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string clientIp,
            byte[] responseBuffer, int bufferSize);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mcp_llm_complete(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string requestJson,
            byte[] responseBuffer, int bufferSize);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mcp_skill_list(
            IntPtr handle, byte[] buffer, int bufferSize);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mcp_skill_execute(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string requestJson,
            byte[] responseBuffer, int bufferSize);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mcp_server_list_remote(
            IntPtr handle, byte[] buffer, int bufferSize);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr mcp_last_error();
    }
}
