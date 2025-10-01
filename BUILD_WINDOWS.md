# Windows Build Instructions

Requirements:

- Visual Studio 2022 or later (C++ workload)
- CMake 3.20+
- vcpkg (recommended) or install dependencies manually

Using vcpkg:

1. Clone repo and bootstrap vcpkg
2. Install needed libs (zlib, openssl, etc.) via vcpkg
3. Configure CMake with vcpkg toolchain file:
   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DUSE_UWS=ON
4. Build and run:
   cmake --build build --config Release
   build\Release\mcp_server.exe

To build with cpp-httplib instead:
   cmake -S . -B build -DUSE_UWS=OFF
