function(xr64_add_release_package_target target)
    if(NOT XR64_CONSUMER_BUILD)
        return()
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(rage_wars_package
        COMMAND "${Python3_EXECUTABLE}"
            "${RAGE_WARS_REPO_ROOT}/scripts/package_rage_wars_release.py"
            --repo-root "${RAGE_WARS_REPO_ROOT}"
            --exe "$<TARGET_FILE:${target}>"
            --runtime-dir "$<TARGET_FILE_DIR:${target}>"
            --sdl2-include "${RAGE_WARS_SDL2_INCLUDE_DIR}"
            --dependencies "${XR64_PC_DEPENDENCIES_DIR}"
            --staging
            --output "${XR64_RELEASE_PACKAGE_DIR}"
        DEPENDS ${target}
        WORKING_DIRECTORY "${RAGE_WARS_REPO_ROOT}"
        COMMENT "Stage the allowlisted Rage Wars consumer package"
        VERBATIM)
    add_test(NAME rage_wars_release_package_contract
        COMMAND "${Python3_EXECUTABLE}"
            "${RAGE_WARS_REPO_ROOT}/scripts/test_package_rage_wars_release.py")
    set_tests_properties(rage_wars_release_package_contract PROPERTIES
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endfunction()