cmake_minimum_required(VERSION 3.16)
project(desktop-notification LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to the host's include directory (contains plugins/PluginABI.h).
if(NOT DEFINED MCP_HOST_INCLUDE)
    set(MCP_HOST_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
endif()

add_library(desktop-notification SHARED
    src/desktop-notification.cpp
    src/NotificationBackend.cpp
)

target_include_directories(desktop-notification PRIVATE
    "${MCP_HOST_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"  # for NotificationBackend.h
)

# Platform-specific link libraries
if(WIN32)
    # runtimeobject.lib provides RoInitialize / RoGetActivationFactory (WRL COM)
    target_link_libraries(desktop-notification PRIVATE runtimeobject)
elseif(APPLE)
    # osascript is part of the macOS base install — no extra link needed
elseif(UNIX)
    # notify-send is a subprocess — no library link needed
endif()

# Place the output binary where NativePluginLoader expects it:
#   plugins/desktop-notification/bin/desktop-notification.dll|.so|.dylib
set_target_properties(desktop-notification PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"  # DLL on Windows
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"  # SO/dylib on Unix
)

# Strip unused symbols on GCC/Clang Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT MSVC)
    target_compile_options(desktop-notification PRIVATE -fvisibility=hidden)
endif()
