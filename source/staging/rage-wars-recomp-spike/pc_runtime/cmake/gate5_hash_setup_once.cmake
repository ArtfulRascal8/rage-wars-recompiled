include("${CMAKE_CURRENT_LIST_DIR}/gate4_setup_once.cmake")

get_property(rage_wars_gate5_hash_scheduled GLOBAL PROPERTY RAGE_WARS_GATE5_HASH_SCHEDULED)
if(rage_wars_gate5_hash_scheduled)
    return()
endif()
set_property(GLOBAL PROPERTY RAGE_WARS_GATE5_HASH_SCHEDULED TRUE)

function(rage_wars_gate5_hash_setup)
    add_executable(rage_wars_rom_xxh3 "${CMAKE_SOURCE_DIR}/tools/rom_xxh3.cpp")
    target_include_directories(rage_wars_rom_xxh3 PRIVATE
        "${CMAKE_SOURCE_DIR}/../../../toolchain/N64ModernRuntime/thirdparty/xxHash")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL rage_wars_gate5_hash_setup)
