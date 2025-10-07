cmake_minimum_required(VERSION 3.15)
project(MCP_Server_CMake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_UWS "Use uWebSockets (async) server" ON)
option(USE_UNITY_BUILD "Enable unity build" ON)
set(TEST_FRAMEWORK "GTest" CACHE STRING "GTest or Catch2")

# set(JSON_Install OFF CACHE INTERNAL "")
# set(JSON_VALIDATOR_INSTALL OFF CACHE INTERNAL "")

include(FetchContent)
include(CMakeFindDependencyMacro)

# json-schema-validator
FetchContent_Declare(
  jsv
  GIT_REPOSITORY https://github.com/pboettch/json-schema-validator.git
  GIT_TAG 2.3.0
)

# nlohmann json
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json jsv)

# cpp-httplib (fallback)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.26.0
)
FetchContent_MakeAvailable(httplib)

# uWebSockets (optional)
if(USE_UWS)
  FetchContent_Declare(
    uwebsockets
    GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
    GIT_TAG v20.47.0
  )
  FetchContent_MakeAvailable(uwebsockets)
endif()

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

if(MSVC)
  add_compile_options(/W4 /WX /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror -fdiagnostics-color=always -pipe)
endif()

# Source files
set(SOURCES
    src/ProtocolHandler.cpp
    src/JsonSchemaValidator.cpp
    src/Logger.cpp
    src/ThreadPool_header.cpp
    src/EchoCommand.cpp
)

if(USE_UWS)
  list(APPEND SOURCES src/UwsServer.cpp)
else()
  list(APPEND SOURCES src/HttplibServer.cpp)
endif()

if(USE_UNITY_BUILD)
  file(WRITE ${CMAKE_BINARY_DIR}/unity_build.cpp "#include \"${PROJECT_SOURCE_DIR}/src/ProtocolHandler.cpp\"\n#include \"${PROJECT_SOURCE_DIR}/src/JsonSchemaValidator.cpp\"\n#include \"${PROJECT_SOURCE_DIR}/src/Logger.cpp\"\n#include \"src/ThreadPool_header.cpp\"\n#include \"${PROJECT_SOURCE_DIR}/src/EchoCommand.cpp\"\n")
  if(USE_UWS)
    file(APPEND ${CMAKE_BINARY_DIR}/unity_build.cpp "#include \"${PROJECT_SOURCE_DIR}/src/UwsServer.cpp\"\n")
  else()
    file(APPEND ${CMAKE_BINARY_DIR}/unity_build.cpp "#include \"${PROJECT_SOURCE_DIR}/src/HttplibServer.cpp\"\n")
  endif()
  set(SOURCES ${CMAKE_BINARY_DIR}/unity_build.cpp)
endif()

add_library(core STATIC ${SOURCES})
target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR}/include)

# Should use these when embedding.
# find_package(nlohmann_json_schema_validator REQUIRED)
# find_package(nlohmann_json 3.11.2 REQUIRED)

target_link_libraries(core PRIVATE
  nlohmann_json::nlohmann_json
  nlohmann_json_schema_validator)

if(USE_UWS)
  target_link_libraries(core PRIVATE uSockets)
else()
  target_link_libraries(core PRIVATE httplib::httplib)
endif()

add_executable(mcp_server src/server_entry.cpp)
target_link_libraries(mcp_server PRIVATE core)

add_executable(demo_client src/demo_client.cpp)
target_link_libraries(demo_client PRIVATE core)

enable_testing()
if(TEST_FRAMEWORK STREQUAL "GTest")
  add_executable(unit_tests tests/ProtocolHandlerTest.cpp)
  target_link_libraries(unit_tests PRIVATE core gtest_main)
  add_test(NAME unit_tests COMMAND unit_tests)
else()
  add_executable(unit_tests tests/ProtocolHandlerTest.cpp)
  target_link_libraries(unit_tests PRIVATE core Catch2::Catch2WithMain)
  add_test(NAME unit_tests COMMAND unit_tests)
endif()
