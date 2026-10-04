cmake_minimum_required(VERSION 3.24)

project(rage_wars_native_contract_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(rage_wars_code_residency_test
    rage_wars_code_residency_test.cpp
    ../src/rage_wars_code_residency.cpp)
target_include_directories(rage_wars_code_residency_test PRIVATE
    ../src
    ../../toolchain/N64Recomp/include)
if(MSVC)
    target_compile_options(rage_wars_code_residency_test PRIVATE /EHsc)
endif()

add_library(rage_wars_code_residency_runtime_compile OBJECT
    ../src/rage_wars_code_residency_runtime.cpp)
target_include_directories(rage_wars_code_residency_runtime_compile PRIVATE
    ../src
    ../../toolchain/N64Recomp/include
    ../../toolchain/N64ModernRuntime/librecomp/include)

add_test(NAME rage_wars_code_residency_contract
    COMMAND rage_wars_code_residency_test)

add_executable(rage_wars_asset_loader_test
    rage_wars_asset_loader_test.cpp
    ../src/rage_wars_asset_loader.cpp)
target_include_directories(rage_wars_asset_loader_test PRIVATE
    ../src)
if(MSVC)
    target_compile_options(rage_wars_asset_loader_test PRIVATE /EHsc)
endif()

add_test(NAME rage_wars_asset_loader_contract
    COMMAND rage_wars_asset_loader_test)

add_executable(rage_wars_scheduler_test
    rage_wars_scheduler_test.cpp
    ../src/rage_wars_scheduler.cpp)
target_include_directories(rage_wars_scheduler_test PRIVATE
    ../src)
if(MSVC)
    target_compile_options(rage_wars_scheduler_test PRIVATE /EHsc)
endif()

add_test(NAME rage_wars_scheduler_contract
    COMMAND rage_wars_scheduler_test)

add_executable(n64_raw_fast3d_segment_test
    n64_raw_fast3d_segment_test.cpp
    ../src/xr64_fast3d/n64_raw_fast3d_renderer.cpp)
target_include_directories(n64_raw_fast3d_segment_test PRIVATE
    ../src)
if(MSVC)
    target_compile_options(n64_raw_fast3d_segment_test PRIVATE /EHsc)
endif()

add_test(NAME n64_raw_fast3d_f3dex2_segment_contract
    COMMAND n64_raw_fast3d_segment_test)
