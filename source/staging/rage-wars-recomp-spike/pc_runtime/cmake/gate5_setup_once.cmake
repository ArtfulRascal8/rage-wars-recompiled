set(RAGE_WARS_REPO_ROOT "${CMAKE_SOURCE_DIR}/../../.." CACHE PATH
    "Standalone XR64 Rage Wars repository root")
set(XR64_RAGE_WARS_RUNTIME_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src" CACHE PATH
    "Rage Wars runtime source directory used by the vendored runtime hooks")

include("${CMAKE_CURRENT_LIST_DIR}/gate5_hash_setup_once.cmake")

option(XR64_DEMO_BUILD "Build portable GUI demo without developer instrumentation" OFF)
option(XR64_CONSUMER_BUILD
    "Build one portable desktop+OpenXR consumer executable and package it" OFF)
option(XR64_PUBLIC_RELEASE
    "Build an audited consumer release with private diagnostic behavior removed" OFF)
if(XR64_PUBLIC_RELEASE AND NOT XR64_CONSUMER_BUILD)
    message(FATAL_ERROR "XR64_PUBLIC_RELEASE requires XR64_CONSUMER_BUILD")
endif()
if(XR64_PUBLIC_RELEASE)
    foreach(private_option XR64_CALL_FAULT_JOURNAL XR64_FAULT_FRAME_CAPTURE XR64_RW_REPLAY_DIAGNOSTICS XR64_RENDER_DIAGNOSTICS)
        if(${private_option})
            message(FATAL_ERROR "XR64_PUBLIC_RELEASE requires ${private_option}=OFF")
        endif()
    endforeach()
endif()
option(XR64_OPENXR "Build opt-in Windows OpenXR prototype" OFF)
option(XR64_AUDIO "Experimental authentic RSP audio path" OFF)
option(XR64_NATIVE_PLAYER_MOVEMENT_SPEED "Compile the opt-in native Player_Tick movement-speed helper" OFF)
option(XR64_NATIVE_PLAYER_IDLE_ENTRY "Compile the opt-in native idle-state entry callback" OFF)
set(XR64_RELEASE_PACKAGE_DIR "" CACHE PATH
    "New or tool-owned external output directory for the portable consumer package")

if(XR64_CONSUMER_BUILD)
    # The GUI ROM loader and OpenXR backend are compiled into the same
    # consumer executable. Keep XR64_DEMO_BUILD for its source filtering and
    # GUI entry path; it no longer disables OpenXR.
    set(XR64_DEMO_BUILD ON CACHE BOOL "Build portable GUI demo without developer instrumentation" FORCE)
    set(XR64_OPENXR ON CACHE BOOL "Build opt-in Windows OpenXR prototype" FORCE)
    set(XR64_AUDIO ON CACHE BOOL "Experimental authentic RSP audio path" FORCE)
    set(XR64_RAGE_WARS_CLEAN_RUN ON CACHE BOOL "Compile out the RW017-RW023 diagnostic and forced-path hooks" FORCE)
    set(XR64_RENDER_DIAGNOSTICS OFF CACHE BOOL
        "Compile renderer tracing, captures, counters, and profiling support" FORCE)
    set(RAGE_WARS_RDRAM_SIZE_BYTES "0x00800000" CACHE STRING
        "Guest-visible RDRAM size: 0x00400000 or 0x00800000" FORCE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING
        "Static MSVC runtime for a portable consumer executable" FORCE)
    set(XR64_CANDIDATE_EXECUTABLE "rage_wars" CACHE STRING
        "Canonical consumer executable output name" FORCE)

    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "XR64_CONSUMER_BUILD requires Windows with MSVC")
    endif()
    if(NOT XR64_RELEASE_PACKAGE_DIR)
        message(FATAL_ERROR "Set XR64_RELEASE_PACKAGE_DIR to an external package directory")
    endif()
    if(NOT IS_ABSOLUTE "${XR64_RELEASE_PACKAGE_DIR}")
        message(FATAL_ERROR "XR64_RELEASE_PACKAGE_DIR must be an absolute path")
    endif()

    set(XR64_VC_CRT_DIR "" CACHE PATH
        "MSVC x64 CRT redistributable directory used for app-local deployment")
    if(NOT XR64_VC_CRT_DIR)
        if(NOT CMAKE_CXX_COMPILER)
            message(FATAL_ERROR
                "Cannot locate the MSVC compiler. Set XR64_VC_CRT_DIR to the x64 Microsoft.VC143.CRT directory.")
        endif()
        get_filename_component(_xr64_msvc_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        foreach(_xr64_parent RANGE 1 6)
            get_filename_component(_xr64_msvc_dir "${_xr64_msvc_dir}" DIRECTORY)
        endforeach()
        set(_xr64_redist_version_file
            "${_xr64_msvc_dir}/Auxiliary/Build/Microsoft.VCRedistVersion.default.txt")
        if(NOT EXISTS "${_xr64_redist_version_file}")
            message(FATAL_ERROR
                "MSVC redist version file is missing: ${_xr64_redist_version_file}. Set XR64_VC_CRT_DIR explicitly.")
        endif()
        file(READ "${_xr64_redist_version_file}" _xr64_redist_version)
        string(STRIP "${_xr64_redist_version}" _xr64_redist_version)
        set(XR64_VC_CRT_DIR
            "${_xr64_msvc_dir}/Redist/MSVC/${_xr64_redist_version}/x64/Microsoft.VC143.CRT"
            CACHE PATH "MSVC x64 CRT redistributable directory used for app-local deployment" FORCE)
    endif()
    foreach(_xr64_vc_crt_dll IN ITEMS msvcp140.dll vcruntime140.dll vcruntime140_1.dll)
        if(NOT EXISTS "${XR64_VC_CRT_DIR}/${_xr64_vc_crt_dll}")
            message(FATAL_ERROR
                "Required MSVC runtime DLL is missing: ${XR64_VC_CRT_DIR}/${_xr64_vc_crt_dll}. Set XR64_VC_CRT_DIR to a complete x64 CRT directory.")
        endif()
    endforeach()

    file(REAL_PATH "${RAGE_WARS_REPO_ROOT}" consumer_repo_root)
    cmake_path(IS_PREFIX consumer_repo_root "${XR64_RELEASE_PACKAGE_DIR}" NORMALIZE package_inside_repo)
    if(package_inside_repo)
        # Temporary package artifacts are permitted inside the active build only.
        file(REAL_PATH "${CMAKE_BINARY_DIR}" package_build_root)
        cmake_path(IS_PREFIX package_build_root "${XR64_RELEASE_PACKAGE_DIR}" NORMALIZE package_inside_build)
        if(NOT package_inside_build)
            message(FATAL_ERROR "XR64_RELEASE_PACKAGE_DIR must be outside the repository or inside this build directory")
        endif()
    endif()
endif()

if(XR64_DEMO_BUILD AND NOT XR64_CONSUMER_BUILD)
    set(XR64_RAGE_WARS_CLEAN_RUN ON CACHE BOOL "" FORCE)
endif()
set(XR64_RAGE_WARS_CLEAN_RUN OFF CACHE BOOL
    "Compile out the RW017-RW023 diagnostic and forced-path hooks")
option(XR64_RENDER_DIAGNOSTICS
    "Compile renderer tracing, captures, counters, and profiling support" ON)
if(XR64_RAGE_WARS_CLEAN_RUN)
    # Keep the existing clean-target option while making renderer diagnostics explicit.
    set(XR64_RENDER_DIAGNOSTICS OFF CACHE BOOL
        "Compile renderer tracing, captures, counters, and profiling support" FORCE)
endif()
set(RAGE_WARS_RDRAM_SIZE_BYTES "0x00400000" CACHE STRING
    "Guest-visible RDRAM size: 0x00400000 or 0x00800000")
if(NOT RAGE_WARS_RDRAM_SIZE_BYTES STREQUAL "0x00400000" AND
        NOT RAGE_WARS_RDRAM_SIZE_BYTES STREQUAL "0x00800000")
    message(FATAL_ERROR
        "RAGE_WARS_RDRAM_SIZE_BYTES must be 0x00400000 or 0x00800000")
endif()

get_property(rage_wars_gate5_scheduled GLOBAL PROPERTY RAGE_WARS_GATE5_SCHEDULED)
if(rage_wars_gate5_scheduled)
    return()
endif()
set_property(GLOBAL PROPERTY RAGE_WARS_GATE5_SCHEDULED TRUE)

function(rage_wars_gate5_setup)
    set(RAGE_WARS_GENERATED_DIR
        "${CMAKE_SOURCE_DIR}/../generated/ares_boot_to_combat"
        CACHE PATH "Generated Rage Wars recomp source directory")
    set(generated_dir "${RAGE_WARS_GENERATED_DIR}")
    set(host_dir "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/recomp_host")
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/pc_dependencies.cmake")
    xr64_prepare_pc_dependencies()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(normalized_dir "${CMAKE_CURRENT_BINARY_DIR}/gate5_generated")
    set(normalized_overlay "${normalized_dir}/recomp_overlays.inl")

    # Gate 5 supersedes the deliberately incomplete Gate 1 header probe and
    # Gate 2 boundary-less link probe. Keep both available for archaeology,
    # but do not let them make the current production default build fail.
    if(TARGET rage_wars_runtime_probe)
        set_property(TARGET rage_wars_runtime_probe PROPERTY EXCLUDE_FROM_ALL TRUE)
    endif()
    if(TARGET rage_wars_link_probe)
        set_property(TARGET rage_wars_link_probe PROPERTY EXCLUDE_FROM_ALL TRUE)
    endif()

    add_custom_command(
        OUTPUT "${normalized_overlay}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${normalized_dir}"
        COMMAND "${Python3_EXECUTABLE}"
            "${RAGE_WARS_REPO_ROOT}/scripts/prepare_recomp_host_overlay.py"
            --input "${generated_dir}/recomp_overlays.inl"
            --output "${normalized_overlay}"
        DEPENDS
            "${generated_dir}/recomp_overlays.inl"
            "${RAGE_WARS_REPO_ROOT}/scripts/prepare_recomp_host_overlay.py"
        VERBATIM)
    add_custom_target(rage_wars_gate5_overlay DEPENDS "${normalized_overlay}")

    add_library(rage_wars_player_view STATIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player/player_view.cpp")
    target_include_directories(rage_wars_player_view PUBLIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_compile_features(rage_wars_player_view PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(rage_wars_player_view PRIVATE /EHsc /fp:strict /W4)
    else()
        target_compile_options(rage_wars_player_view PRIVATE -fno-fast-math -ffp-contract=off)
    endif()

    add_library(rage_wars_player_view_bridge STATIC
        "${host_dir}/src/rage_wars_player_view_bridge.cpp")
    target_include_directories(rage_wars_player_view_bridge PUBLIC
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    target_link_libraries(rage_wars_player_view_bridge PUBLIC rage_wars_player_view)
    if(MSVC)
        # Guest helper failures can unwind across their compatibility C ABI.
        target_compile_options(rage_wars_player_view_bridge PRIVATE /EHsc /EHc- /fp:strict /W4)
    endif()

    add_library(rage_wars_ground_movement STATIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player/ground_movement.cpp")
    target_include_directories(rage_wars_ground_movement PUBLIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_compile_features(rage_wars_ground_movement PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(rage_wars_ground_movement PRIVATE /EHsc /fp:strict /W4)
    else()
        target_compile_options(rage_wars_ground_movement PRIVATE -fno-fast-math -ffp-contract=off)
    endif()

    add_library(rage_wars_ground_movement_bridge STATIC
        "${host_dir}/src/rage_wars_ground_movement_bridge.cpp")
    target_include_directories(rage_wars_ground_movement_bridge PUBLIC
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    target_link_libraries(rage_wars_ground_movement_bridge PUBLIC rage_wars_ground_movement)
    if(MSVC)
        target_compile_options(rage_wars_ground_movement_bridge PRIVATE /EHsc /EHc- /fp:strict /W4)
    endif()

    add_library(rage_wars_player_ease STATIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player/ease_toward.cpp")
    target_include_directories(rage_wars_player_ease PUBLIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_compile_features(rage_wars_player_ease PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(rage_wars_player_ease PRIVATE /EHsc /fp:strict /W4)
    else()
        target_compile_options(rage_wars_player_ease PRIVATE -fno-fast-math -ffp-contract=off)
    endif()

    add_library(rage_wars_player_ease_bridge STATIC
        "${host_dir}/src/rage_wars_player_ease_bridge.cpp")
    target_include_directories(rage_wars_player_ease_bridge PUBLIC
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include"
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_link_libraries(rage_wars_player_ease_bridge PUBLIC rage_wars_player_ease)
    if(MSVC)
        target_compile_options(rage_wars_player_ease_bridge PRIVATE /EHsc /EHc- /fp:strict /W4)
    endif()

    add_library(rage_wars_player_ground_contact STATIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player/ground_contact.cpp")
    target_include_directories(rage_wars_player_ground_contact PUBLIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_compile_features(rage_wars_player_ground_contact PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(rage_wars_player_ground_contact PRIVATE /EHsc /fp:strict /W4)
    endif()

    add_library(rage_wars_player_ground_contact_bridge STATIC
        "${host_dir}/src/rage_wars_player_ground_contact_bridge.cpp")
    target_include_directories(rage_wars_player_ground_contact_bridge PUBLIC
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include"
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_link_libraries(rage_wars_player_ground_contact_bridge PUBLIC rage_wars_player_ground_contact)
    if(MSVC)
        target_compile_options(rage_wars_player_ground_contact_bridge PRIVATE /EHsc /EHc- /fp:strict /W4)
    endif()

    add_library(rage_wars_player_damage_descriptor STATIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player/damage_descriptor.cpp")
    target_include_directories(rage_wars_player_damage_descriptor PUBLIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_compile_features(rage_wars_player_damage_descriptor PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(rage_wars_player_damage_descriptor PRIVATE /EHsc /fp:strict /W4)
    endif()

    add_library(rage_wars_player_damage_descriptor_bridge STATIC
        "${host_dir}/src/rage_wars_player_damage_descriptor_bridge.cpp")
    target_include_directories(rage_wars_player_damage_descriptor_bridge PUBLIC
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include"
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_link_libraries(rage_wars_player_damage_descriptor_bridge PUBLIC
        rage_wars_player_damage_descriptor)
    if(MSVC)
        target_compile_options(rage_wars_player_damage_descriptor_bridge PRIVATE /EHsc /EHc- /fp:strict /W4)
    endif()

    add_library(rage_wars_player_floor_height STATIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player/floor_height.cpp")
    target_include_directories(rage_wars_player_floor_height PUBLIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_compile_features(rage_wars_player_floor_height PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(rage_wars_player_floor_height PRIVATE /EHsc /fp:strict /W4)
    else()
        target_compile_options(rage_wars_player_floor_height PRIVATE -fno-fast-math -ffp-contract=off)
    endif()

    add_library(rage_wars_player_floor_height_bridge STATIC
        "${host_dir}/src/rage_wars_player_floor_height_bridge.cpp")
    target_include_directories(rage_wars_player_floor_height_bridge PUBLIC
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include"
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_link_libraries(rage_wars_player_floor_height_bridge PUBLIC rage_wars_player_floor_height)
    if(MSVC)
        target_compile_options(rage_wars_player_floor_height_bridge PRIVATE /EHsc /EHc- /fp:strict /W4)
    endif()

    add_library(rage_wars_player_surface_lookup STATIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player/surface_lookup.cpp")
    target_include_directories(rage_wars_player_surface_lookup PUBLIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_compile_features(rage_wars_player_surface_lookup PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(rage_wars_player_surface_lookup PRIVATE /EHsc /W4)
    endif()

    add_library(rage_wars_player_surface_lookup_bridge STATIC
        "${host_dir}/src/rage_wars_player_surface_lookup_bridge.cpp")
    target_include_directories(rage_wars_player_surface_lookup_bridge PUBLIC
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include"
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_link_libraries(rage_wars_player_surface_lookup_bridge PUBLIC rage_wars_player_surface_lookup)
    if(MSVC)
        target_compile_options(rage_wars_player_surface_lookup_bridge PRIVATE /EHsc /EHc- /W4)
    endif()

    add_library(rage_wars_player_movement_speed STATIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player/movement_speed.cpp")
    target_include_directories(rage_wars_player_movement_speed PUBLIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_compile_features(rage_wars_player_movement_speed PUBLIC cxx_std_20)
    target_link_libraries(rage_wars_player_movement_speed PUBLIC rage_wars_player_ground_contact)
    if(MSVC)
        target_compile_options(rage_wars_player_movement_speed PRIVATE /EHsc /fp:strict /W4)
    else()
        target_compile_options(rage_wars_player_movement_speed PRIVATE -fno-fast-math -ffp-contract=off)
    endif()

    add_library(rage_wars_player_movement_speed_bridge STATIC
        "${host_dir}/src/rage_wars_player_movement_speed_bridge.cpp")
    target_include_directories(rage_wars_player_movement_speed_bridge PUBLIC
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include"
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_link_libraries(rage_wars_player_movement_speed_bridge PUBLIC rage_wars_player_movement_speed)
    if(MSVC)
        target_compile_options(rage_wars_player_movement_speed_bridge PRIVATE /EHsc /EHc- /fp:strict /W4)
    endif()

    add_library(rage_wars_player_idle_entry STATIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player/idle_entry.cpp")
    target_include_directories(rage_wars_player_idle_entry PUBLIC
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_compile_features(rage_wars_player_idle_entry PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(rage_wars_player_idle_entry PRIVATE /EHsc /fp:strict /W4)
    else()
        target_compile_options(rage_wars_player_idle_entry PRIVATE -fno-fast-math -ffp-contract=off)
    endif()

    add_library(rage_wars_player_idle_entry_bridge STATIC
        "${host_dir}/src/rage_wars_player_idle_entry_bridge.cpp")
    target_include_directories(rage_wars_player_idle_entry_bridge PUBLIC
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include"
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_link_libraries(rage_wars_player_idle_entry_bridge PUBLIC rage_wars_player_idle_entry)
    if(MSVC)
        target_compile_options(rage_wars_player_idle_entry_bridge PRIVATE /EHsc /EHc- /fp:strict /W4)
    endif()

    add_executable(rage_wars_pc
        ${CMAKE_SOURCE_DIR}/src/rage_wars_controller_service.cpp
        ${CMAKE_SOURCE_DIR}/src/rage_wars_input_replay.cpp
        "${CMAKE_SOURCE_DIR}/src/gate5_main.cpp"
        "${CMAKE_SOURCE_DIR}/src/gate5_overlay.cpp"
        "${CMAKE_SOURCE_DIR}/src/rage_wars_asset_loader.cpp"
        "${CMAKE_SOURCE_DIR}/src/rage_wars_code_residency.cpp"
        "${CMAKE_SOURCE_DIR}/src/rage_wars_code_residency_runtime.cpp"
        "${CMAKE_SOURCE_DIR}/src/rage_wars_scheduler.cpp"
        "${CMAKE_SOURCE_DIR}/src/rage_wars_scheduler_runtime.cpp"
        "${CMAKE_SOURCE_DIR}/src/rage_wars_graphics_bridge.cpp"
        "${CMAKE_SOURCE_DIR}/src/rage_wars_godot_live_backend.cpp"
        "${CMAKE_SOURCE_DIR}/src/xr64_fast3d/n64_segment_map.cpp"
        "${CMAKE_SOURCE_DIR}/src/xr64_fast3d/fast3d_scene_decoder.cpp"
        "${CMAKE_SOURCE_DIR}/src/xr64_fast3d/n64_live_fast3d_task_adapter.cpp"
        "${CMAKE_SOURCE_DIR}/src/xr64_fast3d/n64_microcode_registry.cpp"
        "${CMAKE_SOURCE_DIR}/src/xr64_fast3d/n64_raw_fast3d_renderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/xr64_fast3d/render_scene_replay.cpp"
        "${host_dir}/src/main.cpp"
        "${host_dir}/src/rage_wars_crash_report.cpp"
        "${host_dir}/src/rage_wars_native_pc_options.c"
        "${host_dir}/src/rage_wars_native_focus.c"
        "${host_dir}/src/rage_wars_native_actions.c"
        "${host_dir}/src/rage_wars_type1d.cpp"
        "${host_dir}/src/rage_wars_desktop_renderer.cpp"
        "${host_dir}/src/rage_wars_port_options.cpp"
        "${host_dir}/src/rage_wars_modern_look.cpp"
        "${host_dir}/src/runtime_boundary.cpp")
    target_compile_definitions(rage_wars_pc PRIVATE
        XR64_RAGE_WARS_USE_N64MODERNRUNTIME=1
        XR64_RAGE_WARS_USE_N64MODERN_VI_ADAPTERS=1
        XR64_RAGE_WARS_RDRAM_SIZE_BYTES=${RAGE_WARS_RDRAM_SIZE_BYTES}
        XR64_RAGE_WARS_GENERATED_DIR="${generated_dir}")
    if(XR64_RENDER_DIAGNOSTICS)
        target_compile_definitions(rage_wars_pc PRIVATE XR64_RENDER_DIAGNOSTICS=1)
    else()
        target_compile_definitions(rage_wars_pc PRIVATE XR64_RENDER_DIAGNOSTICS=0)
    endif()
    target_compile_definitions(librecomp PRIVATE
        XR64_RAGE_WARS_RDRAM_SIZE_BYTES=${RAGE_WARS_RDRAM_SIZE_BYTES})
    if(XR64_RAGE_WARS_CLEAN_RUN)
        target_compile_definitions(rage_wars_recompiled PRIVATE XR64_RAGE_WARS_CLEAN_RUN=1)
        target_compile_definitions(rage_wars_pc PRIVATE XR64_RAGE_WARS_CLEAN_RUN=1)
        target_compile_definitions(librecomp PRIVATE XR64_RAGE_WARS_CLEAN_RUN=1)
        target_compile_definitions(ultramodern PRIVATE XR64_RAGE_WARS_CLEAN_RUN=1)
    endif()
    add_dependencies(rage_wars_pc rage_wars_gate5_overlay)
    target_link_libraries(rage_wars_pc PRIVATE rage_wars_player_view_bridge rage_wars_ground_movement_bridge
        rage_wars_player_ease_bridge rage_wars_player_ground_contact_bridge
        rage_wars_player_damage_descriptor_bridge rage_wars_player_floor_height_bridge
        rage_wars_player_surface_lookup_bridge rage_wars_player_movement_speed_bridge
        rage_wars_player_idle_entry_bridge)
    target_include_directories(rage_wars_recompiled PRIVATE "${host_dir}/src")
    target_include_directories(rage_wars_pc PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${normalized_dir}"
        "${generated_dir}"
        "${host_dir}/src"
        "${RAGE_WARS_SDL2_INCLUDE_DIR}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64ModernRuntime/librecomp/include")
    if(MSVC)
        target_compile_options(rage_wars_recompiled PRIVATE
            "/FI${host_dir}/src/recomp_memory_shim.h"
            /Zi)
        # Keep a build-local symbol map for native crash RVAs; consumer packages exclude it.
        target_link_options(rage_wars_pc PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/rage_wars_pc.map")
        if(NOT XR64_DEMO_BUILD)
            target_link_options(rage_wars_pc PRIVATE /DEBUG:FULL)
        endif()
        target_compile_options(rage_wars_pc PRIVATE
            "/FI${host_dir}/src/recomp_memory_shim.h"
            /Zi
            $<$<COMPILE_LANGUAGE:CXX>:/W4>)
        set_target_properties(rage_wars_pc PROPERTIES
            PDB_NAME rage_wars_pc
            PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/Release")
    else()
        target_compile_options(rage_wars_recompiled PRIVATE
            -include "${host_dir}/src/recomp_memory_shim.h")
        target_compile_options(rage_wars_pc PRIVATE
            -include "${host_dir}/src/recomp_memory_shim.h")
    endif()
    target_link_libraries(rage_wars_pc PRIVATE
        librecomp
        ultramodern
        opengl32
        dbghelp
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,rage_wars_recompiled>")
    add_custom_command(TARGET rage_wars_pc POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${RAGE_WARS_SDL2_DLL}"
            "$<TARGET_FILE_DIR:rage_wars_pc>/SDL2.dll")
    if(XR64_OPENXR)
        add_executable(rage_wars_xr_controls_test "${CMAKE_SOURCE_DIR}/tests/rage_wars_xr_controls_test.cpp")
        target_include_directories(rage_wars_xr_controls_test PRIVATE "${host_dir}/src")
        target_compile_features(rage_wars_xr_controls_test PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(rage_wars_xr_controls_test PRIVATE /UNDEBUG)
        endif()
        add_test(NAME rage_wars_xr_controls_contract COMMAND rage_wars_xr_controls_test)
        add_executable(rage_wars_xr_camera_test "${CMAKE_SOURCE_DIR}/tests/rage_wars_xr_camera_test.cpp")
        target_include_directories(rage_wars_xr_camera_test PRIVATE "${host_dir}/src" "${CMAKE_SOURCE_DIR}/src")
        target_compile_features(rage_wars_xr_camera_test PRIVATE cxx_std_17)
        add_test(NAME rage_wars_xr_camera_math COMMAND rage_wars_xr_camera_test)
        add_executable(rage_wars_decoded_frame_test "${CMAKE_SOURCE_DIR}/tests/rage_wars_decoded_frame_test.cpp")
        target_include_directories(rage_wars_decoded_frame_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_features(rage_wars_decoded_frame_test PRIVATE cxx_std_17)
        add_test(NAME rage_wars_decoded_frame_ownership COMMAND rage_wars_decoded_frame_test)
        add_executable(rage_wars_presentation_clock_test
            "${CMAKE_SOURCE_DIR}/tests/rage_wars_presentation_clock_test.cpp")
        target_include_directories(rage_wars_presentation_clock_test PRIVATE "${host_dir}/src")
        target_compile_features(rage_wars_presentation_clock_test PRIVATE cxx_std_17)
        add_test(NAME rage_wars_presentation_clock_contract
            COMMAND rage_wars_presentation_clock_test)
        add_executable(rage_wars_body_interpolation_test
            "${CMAKE_SOURCE_DIR}/tests/rage_wars_body_interpolation_test.cpp")
        target_include_directories(rage_wars_body_interpolation_test PRIVATE
            "${host_dir}/src" "${CMAKE_SOURCE_DIR}/src")
        target_compile_features(rage_wars_body_interpolation_test PRIVATE cxx_std_20)
        add_test(NAME rage_wars_body_interpolation_contract
            COMMAND rage_wars_body_interpolation_test)
        add_executable(rage_wars_animation_blend_test
            "${CMAKE_SOURCE_DIR}/tests/rage_wars_animation_blend_test.cpp")
        target_include_directories(rage_wars_animation_blend_test PRIVATE
            "${host_dir}/src" "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/xr64_fast3d")
        target_compile_features(rage_wars_animation_blend_test PRIVATE cxx_std_20)
        add_test(NAME rage_wars_animation_blend_contract
            COMMAND rage_wars_animation_blend_test)
        add_executable(rage_wars_generic_combiner_test
            "${CMAKE_SOURCE_DIR}/tests/rage_wars_generic_combiner_test.cpp")
        target_include_directories(rage_wars_generic_combiner_test PRIVATE
            "${host_dir}/src" "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/xr64_fast3d")
        target_compile_features(rage_wars_generic_combiner_test PRIVATE cxx_std_20)
        add_test(NAME rage_wars_generic_combiner_contract
            COMMAND rage_wars_generic_combiner_test)
        add_executable(rage_wars_clip_equivalence_test
            "${CMAKE_SOURCE_DIR}/tests/n64_clip_equivalence_test.cpp")
        target_include_directories(rage_wars_clip_equivalence_test PRIVATE
            "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/xr64_fast3d")
        target_compile_features(rage_wars_clip_equivalence_test PRIVATE cxx_std_20)
        add_test(NAME rage_wars_clip_equivalence_contract
            COMMAND rage_wars_clip_equivalence_test)
        add_executable(rage_wars_xr_hud_test
            "${CMAKE_SOURCE_DIR}/tests/rage_wars_xr_hud_test.cpp")
        target_include_directories(rage_wars_xr_hud_test PRIVATE
            "${host_dir}/src" "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/xr64_fast3d")
        target_compile_features(rage_wars_xr_hud_test PRIVATE cxx_std_20)
        add_test(NAME rage_wars_xr_hud_contract
            COMMAND rage_wars_xr_hud_test)
        add_executable(rage_wars_xr_laser_test
            "${CMAKE_SOURCE_DIR}/tests/rage_wars_xr_laser_test.cpp")
        target_include_directories(rage_wars_xr_laser_test PRIVATE
            "${host_dir}/src" "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/xr64_fast3d")
        target_compile_features(rage_wars_xr_laser_test PRIVATE cxx_std_20)
        add_test(NAME rage_wars_xr_laser_contract
            COMMAND rage_wars_xr_laser_test)

        target_sources(rage_wars_pc PRIVATE "${host_dir}/src/rage_wars_openxr.cpp")
        target_include_directories(rage_wars_pc PRIVATE "${XR64_OPENXR_INCLUDE_DIR}")
        target_compile_definitions(rage_wars_pc PRIVATE XR64_OPENXR)
        target_link_libraries(rage_wars_pc PRIVATE "${XR64_OPENXR_LIBRARY}")
        add_custom_command(TARGET rage_wars_pc POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${XR64_OPENXR_LOADER}" "$<TARGET_FILE_DIR:rage_wars_pc>/openxr_loader.dll")
    endif()

    if(XR64_CONSUMER_BUILD)
        add_custom_command(TARGET rage_wars_pc POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${XR64_VC_CRT_DIR}/msvcp140.dll"
                "$<TARGET_FILE_DIR:rage_wars_pc>/MSVCP140.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${XR64_VC_CRT_DIR}/vcruntime140.dll"
                "$<TARGET_FILE_DIR:rage_wars_pc>/VCRUNTIME140.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${XR64_VC_CRT_DIR}/vcruntime140_1.dll"
                "$<TARGET_FILE_DIR:rage_wars_pc>/VCRUNTIME140_1.dll"
            VERBATIM)
    endif()

    option(XR64_AUDIO_BASELINE_OUTPUT "Comparison only: retain September 27 output behavior" OFF)
    if(XR64_AUDIO_BASELINE_OUTPUT)
        target_sources(rage_wars_pc PRIVATE "${host_dir}/src/rage_wars_audio_baseline.cpp")
    else()
        target_sources(rage_wars_pc PRIVATE "${host_dir}/src/rage_wars_audio.cpp" "${host_dir}/src/rage_wars_audio_output.cpp")
    endif()
    target_sources(rage_wars_pc PRIVATE "${host_dir}/src/rage_wars_audio_telemetry.cpp")
    if(XR64_AUDIO AND XR64_DEMO_BUILD)
        target_sources(rage_wars_pc PRIVATE "${host_dir}/src/rage_wars_audio_producer.cpp")
    endif()
    add_executable(rage_wars_audio_device_test
        "${CMAKE_SOURCE_DIR}/tests/rage_wars_audio_device_test.cpp"
        "${host_dir}/src/rage_wars_audio.cpp" "${host_dir}/src/rage_wars_audio_output.cpp"
        "${host_dir}/src/rage_wars_audio_telemetry.cpp" "${host_dir}/src/rage_wars_port_options.cpp")
    target_include_directories(rage_wars_audio_device_test PRIVATE "${host_dir}/src" "${RAGE_WARS_SDL2_INCLUDE_DIR}")
    target_compile_definitions(rage_wars_audio_device_test PRIVATE XR64_AUDIO)
    target_compile_options(rage_wars_audio_device_test PRIVATE /EHsc /UNDEBUG)
    add_executable(rage_wars_audio_output_test
        "${CMAKE_SOURCE_DIR}/tests/rage_wars_audio_output_test.cpp"
        "${host_dir}/src/rage_wars_audio_output.cpp" "${host_dir}/src/rage_wars_audio_telemetry.cpp"
        "${host_dir}/src/rage_wars_port_options.cpp")
    target_include_directories(rage_wars_audio_output_test PRIVATE "${host_dir}/src")
    target_compile_options(rage_wars_audio_output_test PRIVATE /EHsc /UNDEBUG)
    add_test(NAME rage_wars_audio_output_contract COMMAND rage_wars_audio_output_test)
    set_tests_properties(rage_wars_audio_output_contract PROPERTIES ENVIRONMENT "XR64_PORT_OPTIONS_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/audio-test-settings.json")
    if(XR64_AUDIO)
        target_compile_definitions(rage_wars_pc PRIVATE XR64_AUDIO)
        target_sources(rage_wars_pc PRIVATE "${RAGE_WARS_REPO_ROOT}/build/audio-rsp-recovery/rage_wars_audio_ucode.cpp")
        set_target_properties(rage_wars_pc PROPERTIES OUTPUT_NAME rage_wars_audio)
    endif()
    set(XR64_CANDIDATE_EXECUTABLE "" CACHE STRING "Optional isolated candidate executable name")
    if(XR64_CANDIDATE_EXECUTABLE)
        set_target_properties(rage_wars_pc PROPERTIES OUTPUT_NAME "${XR64_CANDIDATE_EXECUTABLE}")
    endif()
    if(XR64_CONSUMER_BUILD)
        set_target_properties(rage_wars_pc PROPERTIES OUTPUT_NAME RageWarsRecompiled)
        target_sources(rage_wars_pc PRIVATE "${host_dir}/src/rage_wars_release.rc")
        # Upstream sljit is compiled outside the consumer-source transform.
        # Keep its assertions while removing machine-local __FILE__ prefixes.
        if(MSVC AND CMAKE_C_COMPILER_ID STREQUAL "MSVC" AND TARGET LiveRecomp)
            file(REAL_PATH "${RAGE_WARS_REPO_ROOT}" release_canonical_root)
            file(TO_NATIVE_PATH "${release_canonical_root}" release_native_root)
            target_compile_options(LiveRecomp PRIVATE /experimental:deterministic
                "/pathmap:${release_native_root}=RageWarsRecompiled")
        endif()
        include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/release_package.cmake")
        xr64_add_release_package_target(rage_wars_pc)
    endif()

    if(XR64_DEMO_BUILD)
        target_sources(rage_wars_pc PRIVATE "${host_dir}/src/rage_wars_demo_launcher.cpp")
        target_include_directories(rage_wars_pc PRIVATE "${RAGE_WARS_REPO_ROOT}/toolchain/N64ModernRuntime/thirdparty/xxHash")
        target_link_libraries(rage_wars_pc PRIVATE ole32 uuid comdlg32 gdi32 shell32)
        target_link_options(rage_wars_pc PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /OPT:REF /OPT:ICF /DEBUG:NONE)
        include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/demo_release.cmake")
        foreach(demo_target IN ITEMS rage_wars_pc rage_wars_recompiled librecomp ultramodern)
            xr64_demo_sources(${demo_target})
        endforeach()
    endif()

    add_test(NAME rage_wars_gate5_cli_help COMMAND rage_wars_pc --help)

    add_executable(rage_wars_code_residency_test
        "${CMAKE_SOURCE_DIR}/tests/rage_wars_code_residency_test.cpp"
        "${CMAKE_SOURCE_DIR}/src/rage_wars_code_residency.cpp")
    target_include_directories(rage_wars_code_residency_test PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    if(MSVC)
        target_compile_options(rage_wars_code_residency_test PRIVATE /EHsc)
    endif()
    add_test(NAME rage_wars_code_residency_contract
        COMMAND rage_wars_code_residency_test)

    add_executable(rage_wars_asset_loader_test
        "${CMAKE_SOURCE_DIR}/tests/rage_wars_asset_loader_test.cpp"
        "${CMAKE_SOURCE_DIR}/src/rage_wars_asset_loader.cpp")
    target_include_directories(rage_wars_asset_loader_test PRIVATE
        "${CMAKE_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(rage_wars_asset_loader_test PRIVATE /EHsc)
    endif()
    add_test(NAME rage_wars_asset_loader_contract
        COMMAND rage_wars_asset_loader_test)

    add_executable(rage_wars_controller_service_test
        ${CMAKE_SOURCE_DIR}/tests/rage_wars_controller_service_test.cpp
        ${CMAKE_SOURCE_DIR}/src/rage_wars_controller_service.cpp)
    target_include_directories(rage_wars_controller_service_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(rage_wars_controller_service_test PRIVATE ultramodern)
    if(MSVC)
        target_compile_options(rage_wars_controller_service_test PRIVATE /EHsc /UNDEBUG)
    endif()
    add_test(NAME rage_wars_controller_service_contract
        COMMAND rage_wars_controller_service_test)

    add_executable(rage_wars_input_replay_test
        ${CMAKE_SOURCE_DIR}/tests/rage_wars_input_replay_test.cpp
        ${CMAKE_SOURCE_DIR}/src/rage_wars_input_replay.cpp)
    target_include_directories(rage_wars_input_replay_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(rage_wars_input_replay_test PRIVATE /EHsc /UNDEBUG)
    endif()
    add_test(NAME rage_wars_input_replay_contract
        COMMAND rage_wars_input_replay_test)
    add_executable(rage_wars_player_view_bridge_test
        "${RAGE_WARS_REPO_ROOT}/scripts/player_view_bridge_contract.cpp")
    target_include_directories(rage_wars_player_view_bridge_test PRIVATE
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    target_link_libraries(rage_wars_player_view_bridge_test PRIVATE rage_wars_player_view_bridge)
    if(MSVC)
        target_compile_options(rage_wars_player_view_bridge_test PRIVATE /EHsc /EHc- /fp:strict)
    endif()
    add_test(NAME rage_wars_player_view_original_default COMMAND rage_wars_player_view_bridge_test)
    set_tests_properties(rage_wars_player_view_original_default PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_VIEW=0")
    add_test(NAME rage_wars_player_view_bridge_contract COMMAND rage_wars_player_view_bridge_test enabled)
    set_tests_properties(rage_wars_player_view_bridge_contract PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_VIEW=1")
    add_executable(rage_wars_ground_movement_bridge_test
        "${RAGE_WARS_REPO_ROOT}/scripts/ground_movement_bridge_contract.cpp")
    target_include_directories(rage_wars_ground_movement_bridge_test PRIVATE
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    target_link_libraries(rage_wars_ground_movement_bridge_test PRIVATE rage_wars_ground_movement_bridge)
    if(MSVC)
        target_compile_options(rage_wars_ground_movement_bridge_test PRIVATE /EHsc /EHc- /fp:strict)
    endif()
    add_test(NAME rage_wars_ground_movement_original_default COMMAND rage_wars_ground_movement_bridge_test)
    set_tests_properties(rage_wars_ground_movement_original_default PROPERTIES
        ENVIRONMENT "XR64_NATIVE_GROUND_MOVEMENT=0")
    add_test(NAME rage_wars_ground_movement_bridge_contract COMMAND rage_wars_ground_movement_bridge_test enabled)
    set_tests_properties(rage_wars_ground_movement_bridge_contract PROPERTIES
        ENVIRONMENT "XR64_NATIVE_GROUND_MOVEMENT=1")

    add_executable(rage_wars_player_ease_bridge_test
        "${RAGE_WARS_REPO_ROOT}/scripts/player_ease_bridge_contract.cpp")
    target_include_directories(rage_wars_player_ease_bridge_test PRIVATE
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include"
        "${RAGE_WARS_REPO_ROOT}/xr64/games/rage_wars/native_player")
    target_link_libraries(rage_wars_player_ease_bridge_test PRIVATE rage_wars_player_ease_bridge)
    if(MSVC)
        target_compile_options(rage_wars_player_ease_bridge_test PRIVATE /EHsc /EHc- /fp:strict)
    endif()
    add_test(NAME rage_wars_player_ease_disabled_fallback COMMAND rage_wars_player_ease_bridge_test)
    set_tests_properties(rage_wars_player_ease_disabled_fallback PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_EASE=0")
    add_test(NAME rage_wars_player_ease_bridge_contract COMMAND rage_wars_player_ease_bridge_test enabled)
    set_tests_properties(rage_wars_player_ease_bridge_contract PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_EASE=1")

    add_executable(rage_wars_player_ground_contact_bridge_test
        "${RAGE_WARS_REPO_ROOT}/scripts/player_ground_contact_bridge_contract.cpp")
    target_include_directories(rage_wars_player_ground_contact_bridge_test PRIVATE
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    target_link_libraries(rage_wars_player_ground_contact_bridge_test PRIVATE
        rage_wars_player_ground_contact_bridge)
    if(MSVC)
        target_compile_options(rage_wars_player_ground_contact_bridge_test PRIVATE /EHsc /EHc- /fp:strict)
    endif()
    add_test(NAME rage_wars_player_ground_contact_disabled_fallback
        COMMAND rage_wars_player_ground_contact_bridge_test)
    set_tests_properties(rage_wars_player_ground_contact_disabled_fallback PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_GROUND_CONTACT=0")
    add_test(NAME rage_wars_player_ground_contact_bridge_contract
        COMMAND rage_wars_player_ground_contact_bridge_test enabled)
    set_tests_properties(rage_wars_player_ground_contact_bridge_contract PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_GROUND_CONTACT=1")

    add_executable(rage_wars_player_damage_descriptor_bridge_test
        "${RAGE_WARS_REPO_ROOT}/scripts/player_damage_descriptor_bridge_contract.cpp")
    target_include_directories(rage_wars_player_damage_descriptor_bridge_test PRIVATE
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    target_link_libraries(rage_wars_player_damage_descriptor_bridge_test PRIVATE
        rage_wars_player_damage_descriptor_bridge)
    if(MSVC)
        target_compile_options(rage_wars_player_damage_descriptor_bridge_test PRIVATE /EHsc /EHc- /fp:strict)
    endif()
    add_test(NAME rage_wars_player_damage_descriptor_disabled_fallback
        COMMAND rage_wars_player_damage_descriptor_bridge_test)
    set_tests_properties(rage_wars_player_damage_descriptor_disabled_fallback PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_DAMAGE_DESCRIPTOR=0")
    add_test(NAME rage_wars_player_damage_descriptor_bridge_contract
        COMMAND rage_wars_player_damage_descriptor_bridge_test enabled)
    set_tests_properties(rage_wars_player_damage_descriptor_bridge_contract PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_DAMAGE_DESCRIPTOR=1")

    add_executable(rage_wars_player_floor_height_bridge_test
        "${RAGE_WARS_REPO_ROOT}/scripts/player_floor_height_bridge_contract.cpp")
    target_include_directories(rage_wars_player_floor_height_bridge_test PRIVATE
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    target_link_libraries(rage_wars_player_floor_height_bridge_test PRIVATE
        rage_wars_player_floor_height_bridge)
    if(MSVC)
        target_compile_options(rage_wars_player_floor_height_bridge_test PRIVATE /EHsc /EHc- /fp:strict)
    endif()
    add_test(NAME rage_wars_player_floor_height_disabled_fallback
        COMMAND rage_wars_player_floor_height_bridge_test)
    set_tests_properties(rage_wars_player_floor_height_disabled_fallback PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_FLOOR_HEIGHT=0")
    add_test(NAME rage_wars_player_floor_height_bridge_contract
        COMMAND rage_wars_player_floor_height_bridge_test enabled)
    set_tests_properties(rage_wars_player_floor_height_bridge_contract PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_FLOOR_HEIGHT=1")

    add_executable(rage_wars_player_surface_lookup_bridge_test
        "${RAGE_WARS_REPO_ROOT}/scripts/player_surface_lookup_bridge_contract.cpp")
    target_include_directories(rage_wars_player_surface_lookup_bridge_test PRIVATE
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    target_link_libraries(rage_wars_player_surface_lookup_bridge_test PRIVATE
        rage_wars_player_surface_lookup_bridge)
    if(MSVC)
        target_compile_options(rage_wars_player_surface_lookup_bridge_test PRIVATE /EHsc /EHc-)
    endif()
    add_test(NAME rage_wars_player_surface_lookup_disabled_fallback
        COMMAND rage_wars_player_surface_lookup_bridge_test)
    set_tests_properties(rage_wars_player_surface_lookup_disabled_fallback PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_SURFACE_LOOKUP=0")
    add_test(NAME rage_wars_player_surface_lookup_bridge_contract
        COMMAND rage_wars_player_surface_lookup_bridge_test enabled)
    set_tests_properties(rage_wars_player_surface_lookup_bridge_contract PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_SURFACE_LOOKUP=1")

    add_executable(rage_wars_player_movement_speed_bridge_test
        "${RAGE_WARS_REPO_ROOT}/scripts/player_movement_speed_bridge_contract.cpp")
    target_include_directories(rage_wars_player_movement_speed_bridge_test PRIVATE
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    target_link_libraries(rage_wars_player_movement_speed_bridge_test PRIVATE
        rage_wars_player_movement_speed_bridge)
    if(MSVC)
        target_compile_options(rage_wars_player_movement_speed_bridge_test PRIVATE /EHsc /EHc- /fp:strict)
    endif()
    add_test(NAME rage_wars_player_movement_speed_disabled_fallback
        COMMAND rage_wars_player_movement_speed_bridge_test)
    set_tests_properties(rage_wars_player_movement_speed_disabled_fallback PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_MOVEMENT_SPEED=0")
    add_test(NAME rage_wars_player_movement_speed_bridge_contract
        COMMAND rage_wars_player_movement_speed_bridge_test enabled)
    set_tests_properties(rage_wars_player_movement_speed_bridge_contract PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_MOVEMENT_SPEED=1")

    add_executable(rage_wars_player_idle_entry_bridge_test
        "${RAGE_WARS_REPO_ROOT}/scripts/player_idle_entry_bridge_contract.cpp")
    target_include_directories(rage_wars_player_idle_entry_bridge_test PRIVATE
        "${host_dir}/src" "${generated_dir}"
        "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    target_link_libraries(rage_wars_player_idle_entry_bridge_test PRIVATE
        rage_wars_player_idle_entry_bridge)
    if(MSVC)
        target_compile_options(rage_wars_player_idle_entry_bridge_test PRIVATE /EHsc /EHc- /fp:strict)
    endif()
    add_test(NAME rage_wars_player_idle_entry_disabled_fallback
        COMMAND rage_wars_player_idle_entry_bridge_test)
    set_tests_properties(rage_wars_player_idle_entry_disabled_fallback PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_IDLE_ENTRY=0")
    add_test(NAME rage_wars_player_idle_entry_bridge_contract
        COMMAND rage_wars_player_idle_entry_bridge_test enabled)
    set_tests_properties(rage_wars_player_idle_entry_bridge_contract PROPERTIES
        ENVIRONMENT "XR64_NATIVE_PLAYER_IDLE_ENTRY=1")

    add_executable(rage_wars_controller_mapping_test
        ${CMAKE_SOURCE_DIR}/tests/rage_wars_controller_mapping_test.cpp)
    target_include_directories(rage_wars_controller_mapping_test PRIVATE
        "${host_dir}/src")
    if(MSVC)
        target_compile_options(rage_wars_controller_mapping_test PRIVATE /EHsc /UNDEBUG)
    endif()
    add_test(NAME rage_wars_controller_mapping_contract
        COMMAND rage_wars_controller_mapping_test)
    if(WIN32)
        add_executable(rage_wars_model_filter_test
            "${CMAKE_SOURCE_DIR}/tests/rage_wars_model_filter_test.cpp")
        target_include_directories(rage_wars_model_filter_test PRIVATE
            "${host_dir}/src" "${CMAKE_SOURCE_DIR}/src")
        target_link_libraries(rage_wars_model_filter_test PRIVATE opengl32 gdi32 user32)
        target_compile_features(rage_wars_model_filter_test PRIVATE cxx_std_20)
        target_compile_options(rage_wars_model_filter_test PRIVATE /EHsc)
        add_test(NAME rage_wars_model_filter_gpu_contract COMMAND rage_wars_model_filter_test)
    endif()

    add_executable(rage_wars_native_focus_test
        "${CMAKE_SOURCE_DIR}/tests/rage_wars_native_focus_test.cpp"
        "${host_dir}/src/rage_wars_native_focus.c")
    target_include_directories(rage_wars_native_focus_test PRIVATE
        "${host_dir}/src" "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    target_compile_features(rage_wars_native_focus_test PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(rage_wars_native_focus_test PRIVATE /EHsc /UNDEBUG)
    endif()
    add_test(NAME rage_wars_native_focus_contract COMMAND rage_wars_native_focus_test)

    add_executable(rage_wars_port_options_test
        "${CMAKE_SOURCE_DIR}/tests/rage_wars_port_options_test.cpp"
        "${host_dir}/src/rage_wars_port_options.cpp")
    target_include_directories(rage_wars_port_options_test PRIVATE
        "${host_dir}/src")
    target_compile_features(rage_wars_port_options_test PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(rage_wars_port_options_test PRIVATE /EHsc /UNDEBUG)
    endif()
    add_test(NAME rage_wars_port_options_contract
        COMMAND rage_wars_port_options_test)

    add_executable(rage_wars_weapon_calibration_test "${CMAKE_SOURCE_DIR}/../../../scripts/weapon_calibration_contract.cpp")
    target_include_directories(rage_wars_weapon_calibration_test PRIVATE "${host_dir}/src")
    target_compile_features(rage_wars_weapon_calibration_test PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(rage_wars_weapon_calibration_test PRIVATE /EHsc /UNDEBUG)
    endif()
    add_test(NAME rage_wars_weapon_calibration_contract COMMAND rage_wars_weapon_calibration_test)

    add_executable(rage_wars_modern_controls_test "${CMAKE_SOURCE_DIR}/tests/rage_wars_modern_controls_test.cpp")
    target_include_directories(rage_wars_modern_controls_test PRIVATE "${host_dir}/src")
    target_compile_features(rage_wars_modern_controls_test PRIVATE cxx_std_20)
    add_test(NAME rage_wars_modern_controls_contract COMMAND rage_wars_modern_controls_test)
    add_executable(rage_wars_controls_persistence_test
        "${CMAKE_SOURCE_DIR}/tests/rage_wars_controls_persistence_test.cpp"
        "${host_dir}/src/rage_wars_port_options.cpp")
    target_include_directories(rage_wars_controls_persistence_test PRIVATE "${host_dir}/src")
    target_compile_features(rage_wars_controls_persistence_test PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(rage_wars_modern_controls_test PRIVATE /EHsc)
        target_compile_options(rage_wars_controls_persistence_test PRIVATE /EHsc)
    endif()
    add_test(NAME rage_wars_controls_persistence_write COMMAND rage_wars_controls_persistence_test write)
    add_test(NAME rage_wars_controls_persistence_read COMMAND rage_wars_controls_persistence_test read)
    set_tests_properties(rage_wars_controls_persistence_write PROPERTIES FIXTURES_SETUP controls_config)
    set_tests_properties(rage_wars_controls_persistence_read PROPERTIES FIXTURES_REQUIRED controls_config)
    set_tests_properties(rage_wars_controls_persistence_write rage_wars_controls_persistence_read PROPERTIES
        ENVIRONMENT "XR64_PORT_OPTIONS_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/controls-test-settings.json")
    add_test(NAME rage_wars_controls_legacy_migration COMMAND rage_wars_controls_persistence_test legacy)
    set_tests_properties(rage_wars_controls_legacy_migration PROPERTIES
        ENVIRONMENT "XR64_PORT_OPTIONS_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/controls-legacy-test-settings.json")
    add_executable(rage_wars_guest_clock_test
        "${CMAKE_SOURCE_DIR}/tests/rage_wars_guest_clock_test.cpp")
    target_include_directories(rage_wars_guest_clock_test PRIVATE
        "${host_dir}/src" "${RAGE_WARS_REPO_ROOT}/toolchain/N64Recomp/include")
    add_test(NAME rage_wars_guest_clock_contract COMMAND rage_wars_guest_clock_test)
    add_executable(rage_wars_input_assignment_test
        "${CMAKE_SOURCE_DIR}/tests/rage_wars_input_assignment_test.cpp")
    target_include_directories(rage_wars_input_assignment_test PRIVATE "${host_dir}/src")
    target_compile_features(rage_wars_input_assignment_test PRIVATE cxx_std_20)
    add_test(NAME rage_wars_input_assignment_contract COMMAND rage_wars_input_assignment_test)

    add_executable(rage_wars_scheduler_test
        "${CMAKE_SOURCE_DIR}/tests/rage_wars_scheduler_test.cpp"
        "${CMAKE_SOURCE_DIR}/src/rage_wars_scheduler.cpp")
    target_include_directories(rage_wars_scheduler_test PRIVATE
        "${CMAKE_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(rage_wars_scheduler_test PRIVATE /EHsc)
    endif()
    add_test(NAME rage_wars_scheduler_contract
        COMMAND rage_wars_scheduler_test)
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL rage_wars_gate5_setup)
