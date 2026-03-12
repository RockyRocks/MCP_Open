using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace McpClient
{
    public class LLMProvider
    {
        private readonly IntPtr _handle;
        private const int DefaultBufferSize = 65536;

        internal LLMProvider(IntPtr handle)
        {
            _handle = handle;
        }

        public Task<string> CompleteAsync(string requestJson)
        {
            return Task.Run(() =>
            {
                byte[] buffer = new byte[DefaultBufferSize];
                int result = NativeMethods.mcp_llm_complete(
                    _handle, requestJson, buffer, buffer.Length);
                if (result < 0)
                {
                    IntPtr errPtr = NativeMethods.mcp_last_error();
                    string error = Marshal.PtrToStringUTF8(errPtr) ?? "Unknown error";
                    throw new InvalidOperationException(error);
                }
                return Encoding.UTF8.GetString(buffer, 0, result);
            });
        }
    }
}
