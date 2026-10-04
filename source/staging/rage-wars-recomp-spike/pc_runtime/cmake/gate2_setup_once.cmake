# CMAKE_PROJECT_INCLUDE is evaluated for the root project and every nested
# N64ModernRuntime project. Schedule the Gate 2 layer only once, at the end of
# the root PC project after its dependency targets exist.
get_property(rage_wars_gate2_scheduled GLOBAL PROPERTY RAGE_WARS_GATE2_SCHEDULED)
if(rage_wars_gate2_scheduled)
    return()
endif()
set_property(GLOBAL PROPERTY RAGE_WARS_GATE2_SCHEDULED TRUE)

function(rage_wars_gate2_setup)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    set(RAGE_WARS_GENERATED_DIR
        "${CMAKE_SOURCE_DIR}/../generated/ares_boot_to_combat"
        CACHE PATH "Generated Rage Wars recomp source directory")
    set(rage_wars_generated_dir "${RAGE_WARS_GENERATED_DIR}")
    set(rage_wars_manifest_script
        "${CMAKE_SOURCE_DIR}/tools/build_generated_source_manifest.py")
    set(rage_wars_manifest_cmake
        "${CMAKE_CURRENT_BINARY_DIR}/rage_wars_generated_sources.cmake")
    set(rage_wars_manifest_json
        "${CMAKE_SOURCE_DIR}/cmake/rage_wars_generated_source_manifest.json")

    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${rage_wars_manifest_script}"
            --generated-dir "${rage_wars_generated_dir}"
            --cmake-output "${rage_wars_manifest_cmake}"
            --json-output "${rage_wars_manifest_json}"
            --verify
        RESULT_VARIABLE rage_wars_manifest_result
        OUTPUT_VARIABLE rage_wars_manifest_stdout
        ERROR_VARIABLE rage_wars_manifest_stderr)
    if(NOT rage_wars_manifest_result EQUAL 0)
        message(FATAL_ERROR
            "Rage Wars generated-source manifest validation failed:\n${rage_wars_manifest_stdout}${rage_wars_manifest_stderr}")
    endif()

    set(RAGE_WARS_GENERATED_DIR "${rage_wars_generated_dir}")
    include("${rage_wars_manifest_cmake}")
    add_library(rage_wars_recompiled STATIC ${RAGE_WARS_GENERATED_SOURCES})
    target_include_directories(rage_wars_recompiled PRIVATE
        "${rage_wars_generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64ModernRuntime/librecomp/include")
    target_compile_options(rage_wars_recompiled PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-fno-strict-aliasing>)

    add_executable(rage_wars_recomp_count
        "${CMAKE_SOURCE_DIR}/tests/recomp_output_audit.cpp")
    add_test(
        NAME rage_wars_recomp_count
        COMMAND rage_wars_recomp_count "${rage_wars_generated_dir}")

    add_executable(rage_wars_link_probe
        "${CMAKE_SOURCE_DIR}/src/link_probe_main.cpp")
    target_link_libraries(rage_wars_link_probe PRIVATE
        librecomp
        ultramodern
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,rage_wars_recompiled>")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL rage_wars_gate2_setup)
