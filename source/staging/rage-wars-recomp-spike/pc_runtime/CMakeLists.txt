cmake_minimum_required(VERSION 3.24)

project(rage_wars_pc LANGUAGES C CXX)
if(WIN32 AND XR64_CONSUMER_BUILD)
    enable_language(RC)
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Gate 1 deliberately proves only the local pinned runtime's public headers and
# base libraries. Game-generated sources join this build at Gate 2.
set(RAGE_WARS_N64MODERNRUNTIME_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../toolchain/N64ModernRuntime"
    CACHE PATH "Pinned local N64ModernRuntime checkout")

if(NOT EXISTS "${RAGE_WARS_N64MODERNRUNTIME_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Pinned N64ModernRuntime checkout is missing: ${RAGE_WARS_N64MODERNRUNTIME_DIR}")
endif()

add_subdirectory(
    "${RAGE_WARS_N64MODERNRUNTIME_DIR}"
    "${CMAKE_BINARY_DIR}/n64modernruntime")

add_executable(rage_wars_runtime_probe src/main.cpp)
target_link_libraries(rage_wars_runtime_probe PRIVATE librecomp ultramodern)
