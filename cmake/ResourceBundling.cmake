include(cmake/CMakeRC.cmake)

set(_resources_root ${CMAKE_SOURCE_DIR}/resources)

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
