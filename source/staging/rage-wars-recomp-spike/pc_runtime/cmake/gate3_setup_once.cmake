# Compose Gate 3 verification with the proven Gate 2 native archive layer.
include("${CMAKE_CURRENT_LIST_DIR}/gate2_setup_once.cmake")

get_property(rage_wars_gate3_scheduled GLOBAL PROPERTY RAGE_WARS_GATE3_SCHEDULED)
if(rage_wars_gate3_scheduled)
    return()
endif()
set_property(GLOBAL PROPERTY RAGE_WARS_GATE3_SCHEDULED TRUE)
# enable_testing must execute in directory scope, not inside the deferred function.
enable_testing()

function(rage_wars_gate3_setup)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(rage_wars_exclusion_audit
        "${CMAKE_SOURCE_DIR}/tools/build_exclusion_audit.py")

    add_custom_target(rage_wars_gate3_exclusion_audit
        COMMAND "${Python3_EXECUTABLE}" "${rage_wars_exclusion_audit}" --check
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Verifying all 86 Rage Wars Gate 3 exclusions"
        VERBATIM)

    add_test(
        NAME rage_wars_exclusion_audit
        COMMAND "${Python3_EXECUTABLE}" "${rage_wars_exclusion_audit}" --check)
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL rage_wars_gate3_setup)
