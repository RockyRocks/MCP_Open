cmake_minimum_required(VERSION 3.16)
project(example-plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to the host's include directory (contains plugins/PluginABI.h).
# When built as part of the host: resolved via target_include_directories.
# When built standalone: pass -DMCP_HOST_INCLUDE=<abs-path> on the cmake line.
if(NOT DEFINED MCP_HOST_INCLUDE)
    # Try a relative path suitable for in-tree builds
    set(MCP_HOST_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
endif()

add_library(example-plugin SHARED src/example-plugin.cpp)

target_include_directories(example-plugin PRIVATE "${MCP_HOST_INCLUDE}")

# Place the output binary in bin/ beside the source tree so NativePluginLoader
# can find it at <plugins-dir>/example-plugin/bin/example-plugin.dll|.so
set_target_properties(example-plugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"  # DLL (Windows)
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"  # SO (Unix)
)

# Export all symbols on MSVC (shared lib default on Windows is hidden)
if(MSVC)
    # The plugin uses MCP_PLUGIN_EXPORT (__declspec(dllexport)) explicitly,
    # so we do not need /EXPORT linker flags here.
endif()

# Strip unused symbols on Release builds (Unix only)
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT MSVC)
    target_compile_options(example-plugin PRIVATE -fvisibility=hidden)
endif()
