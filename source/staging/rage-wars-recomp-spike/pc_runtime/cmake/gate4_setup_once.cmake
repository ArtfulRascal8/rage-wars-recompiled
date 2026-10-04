# Compose Gate 4 address-state verification with the Gate 2/3 pipeline.
include("${CMAKE_CURRENT_LIST_DIR}/gate3_setup_once.cmake")

get_property(rage_wars_gate4_scheduled GLOBAL PROPERTY RAGE_WARS_GATE4_SCHEDULED)
if(rage_wars_gate4_scheduled)
    return()
endif()
set_property(GLOBAL PROPERTY RAGE_WARS_GATE4_SCHEDULED TRUE)

function(rage_wars_gate4_setup)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(rage_wars_address_audit
        "${CMAKE_SOURCE_DIR}/tools/build_address_map_audit.py")

    add_custom_target(rage_wars_gate4_address_audit
        COMMAND "${Python3_EXECUTABLE}" "${rage_wars_address_audit}" --check
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Verifying Rage Wars Gate 4 address, TLB, DMA, and overlay state"
        VERBATIM)

    add_test(
        NAME rage_wars_address_map_audit
        COMMAND "${Python3_EXECUTABLE}" "${rage_wars_address_audit}" --check)
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL rage_wars_gate4_setup)
