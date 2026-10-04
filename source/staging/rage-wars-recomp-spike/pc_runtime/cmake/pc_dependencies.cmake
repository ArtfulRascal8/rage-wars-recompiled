# Public, pinned desktop dependencies; never fall back to a donor checkout.
function(xr64_prepare_pc_dependencies)
    foreach(legacy RAGE_WARS_SDL2_INCLUDE_DIR RAGE_WARS_SDL2_DLL XR64_OPENXR_SDK XR64_OPENXR_LOADER)
        if(DEFINED CACHE{${legacy}} AND NOT "$CACHE{${legacy}}" STREQUAL "")
            message(FATAL_ERROR "Legacy dependency override ${legacy} is unsupported. Use a fresh build and XR64_PC_DEPENDENCIES_DIR.")
        endif()
    endforeach()
    set(XR64_PC_DEPENDENCIES_DIR "${CMAKE_BINARY_DIR}/pc-dependencies" CACHE PATH
        "Owned cache for hash-pinned public SDL2 and OpenXR dependencies")
    option(XR64_DEPENDENCIES_OFFLINE "Require pinned archives already in the cache" OFF)
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${RAGE_WARS_REPO_ROOT}/scripts/pc_dependencies.lock.json"
        "${RAGE_WARS_REPO_ROOT}/scripts/prepare_pc_dependencies.py")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(offline_arg)
    if(XR64_DEPENDENCIES_OFFLINE)
        set(offline_arg --offline)
    endif()
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${RAGE_WARS_REPO_ROOT}/scripts/prepare_pc_dependencies.py"
        --output "${XR64_PC_DEPENDENCIES_DIR}" --cmake "${CMAKE_COMMAND}" ${offline_arg}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Pinned PC dependency preparation failed: ${output}\n${errors}")
    endif()
    include("${XR64_PC_DEPENDENCIES_DIR}/dependencies.cmake")
    foreach(variable RAGE_WARS_SDL2_INCLUDE_DIR RAGE_WARS_SDL2_DLL XR64_OPENXR_INCLUDE_DIR XR64_OPENXR_LIBRARY XR64_OPENXR_LOADER)
        set(${variable} "${${variable}}" PARENT_SCOPE)
    endforeach()
endfunction()
