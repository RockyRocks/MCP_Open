cmake_minimum_required(VERSION 3.16)
project(unreal-tools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to the host's include directory (contains plugins/PluginABI.h).
# When built as part of the host: resolved via target_include_directories.
# When built standalone: pass -DMCP_HOST_INCLUDE=<abs-path> on the cmake line.
if(NOT DEFINED MCP_HOST_INCLUDE)
    set(MCP_HOST_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
endif()

add_library(unreal-tools SHARED src/unreal-tools.cpp)

target_include_directories(unreal-tools PRIVATE "${MCP_HOST_INCLUDE}")

# Place the output binary in bin/ beside the source tree so NativePluginLoader
# can find it at <plugins-dir>/unreal-tools/bin/unreal-tools.dll|.so
set_target_properties(unreal-tools PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"  # DLL (Windows)
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"  # SO (Unix)
)

# Strip unused symbols on Release builds (Unix only)
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT MSVC)
    target_compile_options(unreal-tools PRIVATE -fvisibility=hidden)
endif()
