using System;
using System.Runtime.InteropServices;
using System.Text;

namespace McpClient
{
    public class McpServer : IDisposable
    {
        private IntPtr _handle;
        private bool _disposed;
        private const int DefaultBufferSize = 65536;

        public McpServer(string? configPath = null)
        {
            _handle = NativeMethods.mcp_server_create(configPath);
            if (_handle == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    $"Failed to create MCP server: {GetLastError()}");
            }
        }

        public CommandRegistry Commands => new CommandRegistry(_handle);
        public LLMProvider LLM => new LLMProvider(_handle);

        public string HandleRequest(string jsonBody, string clientIp = "127.0.0.1")
        {
            ThrowIfDisposed();
            byte[] buffer = new byte[DefaultBufferSize];
            int result = NativeMethods.mcp_handle_request(
                _handle, jsonBody, clientIp, buffer, buffer.Length);
            if (result < 0)
                throw new InvalidOperationException(GetLastError());
            return Encoding.UTF8.GetString(buffer, 0, result);
        }

        public string ListSkills()
        {
            ThrowIfDisposed();
            byte[] buffer = new byte[DefaultBufferSize];
            int result = NativeMethods.mcp_skill_list(_handle, buffer, buffer.Length);
            if (result < 0)
                throw new InvalidOperationException(GetLastError());
            return Encoding.UTF8.GetString(buffer, 0, result);
        }

        public string ExecuteSkill(string requestJson)
        {
            ThrowIfDisposed();
            byte[] buffer = new byte[DefaultBufferSize];
            int result = NativeMethods.mcp_skill_execute(
                _handle, requestJson, buffer, buffer.Length);
            if (result < 0)
                throw new InvalidOperationException(GetLastError());
            return Encoding.UTF8.GetString(buffer, 0, result);
        }

        public string ListRemoteServers()
        {
            ThrowIfDisposed();
            byte[] buffer = new byte[DefaultBufferSize];
            int result = NativeMethods.mcp_server_list_remote(
                _handle, buffer, buffer.Length);
            if (result < 0)
                throw new InvalidOperationException(GetLastError());
            return Encoding.UTF8.GetString(buffer, 0, result);
        }

        private static string GetLastError()
        {
            IntPtr errPtr = NativeMethods.mcp_last_error();
            return Marshal.PtrToStringUTF8(errPtr) ?? "Unknown error";
        }

        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(McpServer));
        }

        public void Dispose()
        {
            if (!_disposed)
            {
                if (_handle != IntPtr.Zero)
                {
                    NativeMethods.mcp_server_destroy(_handle);
                    _handle = IntPtr.Zero;
                }
                _disposed = true;
            }
            GC.SuppressFinalize(this);
        }

        ~McpServer() => Dispose();
    }
}
