cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0077 NEW)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
project(MCP_Server_CMake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build all static libraries with -fPIC so they can be linked into shared libs (mcp_capi)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(USE_UWS "Use uWebSockets (async) server" OFF)
option(BUILD_CSHARP "Build C# wrapper (requires dotnet SDK)" ON)
set(TEST_FRAMEWORK "GTest" CACHE STRING "GTest or Catch2")

include(FetchContent)
include(CMakeFindDependencyMacro)

# ------------------------------------------------------------------------------
# json-schema-validator
# ------------------------------------------------------------------------------
FetchContent_Declare(
  jsv
  GIT_REPOSITORY https://github.com/pboettch/json-schema-validator.git
  GIT_TAG 2.3.0
)

# ------------------------------------------------------------------------------
# nlohmann json
# ------------------------------------------------------------------------------
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json jsv)

# ------------------------------------------------------------------------------
# cpp-httplib
# ------------------------------------------------------------------------------
# Disable httplib optional dependency auto-detection.
# The _IF_AVAILABLE flags cause httplib to find & link zstd/brotli/zlib/openssl
# when headers happen to be present on the system — but the CMake targets may
# not exist, leading to "target not found" errors (e.g. zstd::libzstd on CI).
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.26.0
)
FetchContent_MakeAvailable(httplib)

# ------------------------------------------------------------------------------
# uWebSockets (optional)
# ------------------------------------------------------------------------------
if(USE_UWS)
  message(STATUS "Configuring uWebSockets support...")
  FetchContent_Declare(
    uwebsockets
    GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
    GIT_TAG v20.47.0
  )
  FetchContent_MakeAvailable(uwebsockets)

  add_library(uwebsockets INTERFACE)
  target_include_directories(uwebsockets INTERFACE
    ${uwebsockets_SOURCE_DIR}/src
    ${uwebsockets_SOURCE_DIR}/uSockets/src
  )
  target_compile_definitions(uwebsockets INTERFACE UWS_NO_ZLIB)
  if(NOT MSVC)
    target_compile_options(uwebsockets INTERFACE -Wall -O3)
  endif()
endif()

# ------------------------------------------------------------------------------
# googletest / catch2
# ------------------------------------------------------------------------------
if(TEST_FRAMEWORK STREQUAL "GTest")
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  )
  FetchContent_MakeAvailable(googletest)
else()
  FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
  )
  FetchContent_MakeAvailable(catch2)
endif()

# ------------------------------------------------------------------------------
# Compiler options
# ------------------------------------------------------------------------------
if(MSVC)
  add_compile_options(/W4 /WX /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror -fdiagnostics-color=always -pipe)
endif()

# ------------------------------------------------------------------------------
# Core static library
# ------------------------------------------------------------------------------
set(CORE_SOURCES
    src/core/ProtocolHandler.cpp
    src/core/Config.cpp
    src/core/Logger.cpp
    src/core/ThreadPool.cpp
    src/validation/JsonSchemaValidator.cpp
    src/validation/InputSanitizer.cpp
    src/commands/CommandRegistry.cpp
    src/commands/EchoCommand.cpp
    src/llm/LiteLLMProvider.cpp
    src/llm/LLMCommand.cpp
    src/discovery/McpServerRegistry.cpp
    src/discovery/CompositeCommand.cpp
    src/skills/SkillEngine.cpp
    src/skills/SkillCommand.cpp
    src/http/HttplibClient.cpp
    src/security/RateLimiter.cpp
    src/security/ApiKeyValidator.cpp
    src/security/SecurityHeaders.cpp
    src/server/HttplibServer.cpp
    src/server/StdioTransport.cpp
)

if(USE_UWS)
  list(APPEND CORE_SOURCES src/server/UwsServer.cpp)
endif()

add_library(core STATIC ${CORE_SOURCES})
target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(core PUBLIC
  nlohmann_json::nlohmann_json
  nlohmann_json_schema_validator
  httplib::httplib
)

if(USE_UWS)
  target_link_libraries(core PUBLIC uwebsockets)
  target_compile_definitions(core PUBLIC USE_UWS=1)
endif()

# std::filesystem requires explicit linking on GCC < 9 and Clang < 9
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(core PUBLIC stdc++fs)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(core PUBLIC c++fs)
endif()

# pthreads on Linux/Unix
if(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(core PUBLIC Threads::Threads)
endif()

# macOS: httplib uses CoreFoundation + CFNetwork for async DNS resolution
if(APPLE)
  target_link_libraries(core PUBLIC "-framework CoreFoundation" "-framework CFNetwork")
endif()

# ------------------------------------------------------------------------------
# Server executable
# ------------------------------------------------------------------------------
add_executable(mcp_server src/server_entry.cpp)
target_link_libraries(mcp_server PRIVATE core)

# ------------------------------------------------------------------------------
# Demo client
# ------------------------------------------------------------------------------
add_executable(demo_client src/demo_client.cpp)
target_link_libraries(demo_client PRIVATE core)

# ------------------------------------------------------------------------------
# C API shared library
# ------------------------------------------------------------------------------
add_library(mcp_capi SHARED capi/src/mcp_capi.cpp)
target_include_directories(mcp_capi
  PUBLIC capi/include
  PRIVATE ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(mcp_capi PRIVATE core)
target_compile_definitions(mcp_capi PRIVATE MCP_CAPI_EXPORTS)

# ------------------------------------------------------------------------------
# C# wrapper (built via dotnet, triggered from CMake)
# ------------------------------------------------------------------------------
if(BUILD_CSHARP)
  find_program(DOTNET_EXE dotnet)
  if(DOTNET_EXE)
    add_custom_target(mcp_csharp ALL
      COMMAND ${DOTNET_EXE} build ${CMAKE_SOURCE_DIR}/csharp/McpClient.csproj
              -c $<IF:$<CONFIG:Debug>,Debug,Release>
              -o ${CMAKE_BINARY_DIR}/csharp
      DEPENDS mcp_capi
      COMMENT "Building C# wrapper (McpClient)"
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/csharp
    )
  else()
    message(STATUS "dotnet SDK not found, skipping C# wrapper build")
  endif()
endif()

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
enable_testing()

set(TEST_SOURCES
    tests/ProtocolHandler_test.cpp
    tests/CommandRegistry_test.cpp
    tests/InputSanitizer_test.cpp
    tests/RateLimiter_test.cpp
    tests/Config_test.cpp
    tests/SkillEngine_test.cpp
    tests/McpServerRegistry_test.cpp
    tests/StdioTransport_test.cpp
)

if(TEST_FRAMEWORK STREQUAL "GTest")
  add_executable(unit_tests ${TEST_SOURCES})
  target_link_libraries(unit_tests PRIVATE core gtest_main)
  add_test(NAME unit_tests COMMAND unit_tests)
else()
  add_executable(unit_tests ${TEST_SOURCES})
  target_link_libraries(unit_tests PRIVATE core Catch2::Catch2WithMain)
  add_test(NAME unit_tests COMMAND unit_tests)
endif()
