cmake_minimum_required(VERSION 3.20)

project(ultramodern)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ultramodern STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/error_handling.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/events.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/extensions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/input.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mesgqueue.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/misc_ultra.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer_context.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rsp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scheduling.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/task_win32.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/threadqueue.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/threads.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/timer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ultrainit.cpp"
)

target_include_directories(ultramodern PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    "${PROJECT_SOURCE_DIR}/../thirdparty"
    "${PROJECT_SOURCE_DIR}/../thirdparty/concurrentqueue"
    "${PROJECT_SOURCE_DIR}/../thirdparty/sse2neon"
)

set(XR64_RAGE_WARS_RUNTIME_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../pc_runtime/src"
    CACHE PATH "Rage Wars runtime source directory for project-specific scheduler hooks")
if(NOT IS_DIRECTORY "${XR64_RAGE_WARS_RUNTIME_SOURCE_DIR}")
    message(FATAL_ERROR
        "XR64_RAGE_WARS_RUNTIME_SOURCE_DIR must contain the Rage Wars runtime sources")
endif()
target_include_directories(ultramodern PRIVATE
    "${XR64_RAGE_WARS_RUNTIME_SOURCE_DIR}")

target_compile_options(ultramodern PRIVATE
#    -Wall
#    -Wextra
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wno-unused-parameter>
)

if (WIN32)
    add_compile_definitions(NOMINMAX)
endif()
