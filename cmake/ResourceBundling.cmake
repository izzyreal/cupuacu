include(cmake/CMakeRC.cmake)

set(_resources_root ${CMAKE_SOURCE_DIR}/resources)
set(_test_resources_root ${CMAKE_SOURCE_DIR}/test-resources)

function(_bundle_resources _target_name)

    if(NOT TARGET resources)
        file(GLOB_RECURSE RESOURCES "${_resources_root}/*")
        list(FILTER RESOURCES EXCLUDE REGEX "\\.DS_Store$")
        # Application icons are packaged separately by each platform and are
        # not opened through ResourceUtil. Avoid embedding a second
        # cross-platform copy of them in every executable.
        list(FILTER RESOURCES EXCLUDE REGEX "/app-icons/")
        # Source license files are combined into the generated, user-facing
        # THIRD_PARTY_NOTICES resource.
        list(FILTER RESOURCES EXCLUDE REGEX "/licenses/")

        cmrc_add_resource_library(
                resources
                ALIAS cupuacu::rc
                NAMESPACE cupuacu
                WHENCE ${_resources_root}
                ${RESOURCES}
        )
    endif()

    target_link_libraries(${_target_name} PUBLIC cupuacu::rc)

    if(CUPUACU_GENERATED_RESOURCE_FILES)
        if(NOT TARGET generated_resources)
            cmrc_add_resource_library(
                    generated_resources
                    ALIAS cupuacu::generated_rc
                    NAMESPACE cupuacu_generated
                    WHENCE ${CUPUACU_GENERATED_RESOURCES_ROOT}
                    ${CUPUACU_GENERATED_RESOURCE_FILES}
            )
        endif()
        target_link_libraries(${_target_name} PUBLIC cupuacu::generated_rc)
    endif()
endfunction()

function(_bundle_test_resources _target_name)
    if(NOT EXISTS "${_test_resources_root}")
        return()
    endif()

    file(GLOB_RECURSE TEST_RESOURCES "${_test_resources_root}/*")
    list(FILTER TEST_RESOURCES EXCLUDE REGEX "\\.DS_Store$")
    if(TEST_RESOURCES STREQUAL "")
        return()
    endif()

    if(NOT TARGET test_resources)
        cmrc_add_resource_library(
                test_resources
                ALIAS cupuacu::test_rc
                NAMESPACE cupuacu_test
                WHENCE ${_test_resources_root}
                ${TEST_RESOURCES}
        )
    endif()

    target_link_libraries(${_target_name} PUBLIC test_resources)
endfunction()
