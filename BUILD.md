# Build Instructions

Cross-platform build guide for MCP_Server_CMake covering Windows, Linux, and macOS.

---

## Prerequisites

### All Platforms

- CMake 3.15 or later
- C++20 compatible compiler
- Git (for FetchContent dependency downloads)
- (Optional) .NET 8.0 SDK — required only if `BUILD_CSHARP=ON`
- (Optional) Python 3.8+ with pip — required only for LiteLLM proxy

### Windows

- Visual Studio 2022 (v17.0+) with the **Desktop development with C++** workload
- MSVC v143 toolset or later (ships with VS 2022)

### Linux

- GCC 10+ or Clang 10+ (C++20 support required)
- `build-essential` (Debian/Ubuntu) or equivalent
- pthreads (typically included with libc)

```bash
# Debian / Ubuntu
sudo apt update && sudo apt install -y build-essential cmake git

# Fedora / RHEL
sudo dnf install -y gcc-c++ cmake git make
```

### macOS

- Xcode 14+ or Command Line Tools (Apple Clang 14+ for C++20)
- Homebrew recommended for CMake if not using Xcode's bundled version

```bash
xcode-select --install
brew install cmake
```

---

## CMake Options

| Option | Type | Default | Description |
|---|---|---|---|
| `USE_UWS` | BOOL | `OFF` | Use uWebSockets async server instead of cpp-httplib |
| `BUILD_CSHARP` | BOOL | `ON` | Build C# wrapper via `dotnet build` (skipped if dotnet SDK not found) |
| `TEST_FRAMEWORK` | STRING | `GTest` | Test framework: `GTest` or `Catch2` |

---

## Build Targets

| Target | Type | Description |
|---|---|---|
| `core` | Static library | Core MCP server library (all modules) |
| `mcp_server` | Executable | Main server entry point |
| `demo_client` | Executable | Demo client for testing |
| `mcp_capi` | Shared library | C API shared library for interop (P/Invoke) |
| `mcp_csharp` | Custom target | C# wrapper (`McpClient.dll`), depends on `mcp_capi` |
| `unit_tests` | Executable | All unit tests (GTest or Catch2) |

---

## Building

### Windows (MSVC)

```powershell
# Configure (from MCP_Open directory)
cmake -S . -B build

# Build (Release)
cmake --build build --config Release

# Build (Debug)
cmake --build build --config Debug

# Run server
build\Release\mcp_server.exe
```

With optional flags:

```powershell
cmake -S . -B build -DUSE_UWS=ON -DBUILD_CSHARP=OFF -DTEST_FRAMEWORK=Catch2
cmake --build build --config Release
```

Using vcpkg (if you prefer vcpkg-managed dependencies):

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Linux (GCC / Clang)

```bash
# Configure
cmake -S . -B build

# Build
cmake --build build -j$(nproc)

# Run server
./build/mcp_server
```

To use Clang instead of GCC:

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j$(nproc)
```

With optional flags:

```bash
cmake -S . -B build -DUSE_UWS=ON -DBUILD_CSHARP=OFF -DTEST_FRAMEWORK=Catch2
cmake --build build -j$(nproc)
```

### macOS (Apple Clang)

```bash
# Configure
cmake -S . -B build

# Build
cmake --build build -j$(sysctl -n hw.ncpu)

# Run server
./build/mcp_server
```

With optional flags:

```bash
cmake -S . -B build -DUSE_UWS=ON -DBUILD_CSHARP=OFF
cmake --build build -j$(sysctl -n hw.ncpu)
```

---

## Running Tests

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run all tests (Windows, specify config)
ctest --test-dir build -C Release --output-on-failure

# Run the test executable directly for verbose output
./build/unit_tests          # Linux / macOS
build\Release\unit_tests.exe  # Windows
```

---

## Building Individual Targets

```bash
# Build only the core library
cmake --build build --target core

# Build only the C API shared library
cmake --build build --target mcp_capi

# Build only the C# wrapper
cmake --build build --target mcp_csharp

# Build only tests
cmake --build build --target unit_tests
```

---

## C# Wrapper

The C# wrapper requires .NET 8.0 SDK. CMake will automatically detect `dotnet` and build the wrapper if `BUILD_CSHARP=ON` (default). If the SDK is not found, the build skips the C# target with a status message.

```bash
# Verify dotnet is available
dotnet --version

# Build with C# wrapper explicitly enabled
cmake -S . -B build -DBUILD_CSHARP=ON
cmake --build build --target mcp_csharp
```

Output: `build/csharp/McpClient.dll`

---

## LiteLLM Proxy Setup

The LLM subsystem sends requests to a LiteLLM proxy sidecar. Start the proxy before using LLM or skill commands.

### Linux / macOS

```bash
cd litellm
chmod +x start_proxy.sh
./start_proxy.sh
```

### Windows

```powershell
cd litellm
.\start_proxy.ps1
```

Set API keys via environment variables before starting:

```bash
export ANTHROPIC_API_KEY=sk-ant-...
export OPENAI_API_KEY=sk-...
export LITELLM_MASTER_KEY=sk-litellm-...
```

The proxy listens on `http://localhost:4000` by default (configurable in `config/mcp_config.json`).

---

## Configuration

| File | Purpose |
|---|---|
| `config/mcp_config.json.example` | Server settings (port, LiteLLM URL, model, thread pool, rate limits, auth) |
| `config/mcp_servers.json.example` | MCP server discovery registry (remote server endpoints + capabilities) |
| `skills/*.json` | Skill definitions (prompt templates, models, parameters) |
| `litellm/litellm_config.yaml` | LiteLLM proxy model routing configuration |

Copy `.example` files and customize:

```bash
cp config/mcp_config.json.example config/mcp_config.json
cp config/mcp_servers.json.example config/mcp_servers.json
```

---

## Troubleshooting

### CMake policy warning with nlohmann/json

If you see `Compatibility with CMake < 3.5 has been removed`, the project already handles this via `CMAKE_POLICY_VERSION_MINIMUM`. Ensure you are using CMake 3.15+.

### MSVC `getenv` deprecation warning

Handled internally via `_dupenv_s` on MSVC. No action needed.

### Linux: `std::filesystem` linker error (GCC < 9)

The CMakeLists.txt automatically links `stdc++fs` on GCC < 9 and `c++fs` on Clang < 9. If you still see errors, upgrade to GCC 10+ or Clang 10+.

### Linux: Missing pthreads

The CMakeLists.txt automatically finds and links pthreads on Unix. If `find_package(Threads)` fails, install your distro's threading library.

### C# build skipped

If you see `dotnet SDK not found, skipping C# wrapper build`, install the .NET 8.0 SDK and ensure `dotnet` is on your PATH.
