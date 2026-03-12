using System;
using System.Runtime.InteropServices;
using System.Text;

namespace McpClient
{
    public class CommandRegistry
    {
        private readonly IntPtr _handle;
        private const int DefaultBufferSize = 4096;

        internal CommandRegistry(IntPtr handle)
        {
            _handle = handle;
        }

        public string ListCommands()
        {
            byte[] buffer = new byte[DefaultBufferSize];
            int result = NativeMethods.mcp_command_list(
                _handle, buffer, buffer.Length);
            if (result < 0)
            {
                IntPtr errPtr = NativeMethods.mcp_last_error();
                string error = Marshal.PtrToStringUTF8(errPtr) ?? "Unknown error";
                throw new InvalidOperationException(error);
            }
            return Encoding.UTF8.GetString(buffer, 0, result);
        }

        public bool HasCommand(string name)
        {
            int result = NativeMethods.mcp_command_has(_handle, name);
            return result == 1;
        }
    }
}
