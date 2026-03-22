include(cmake/CMakeRC.cmake)

set(_resources_root ${CMAKE_SOURCE_DIR}/resources)
set(_test_resources_root ${CMAKE_SOURCE_DIR}/test-resources)

function(_bundle_resources _target_name)

    file(GLOB_RECURSE RESOURCES "${_resources_root}/*")
    list(FILTER RESOURCES EXCLUDE REGEX "\\.DS_Store$")

    cmrc_add_resource_library(
            resources 
            ALIAS cupuacu::rc
            NAMESPACE cupuacu 
            WHENCE ${_resources_root}
            ${RESOURCES}
    )

    target_link_libraries(${_target_name} PUBLIC cupuacu::rc)
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

    target_link_libraries(${_target_name} PUBLIC cupuacu::test_rc)
endfunction()
