include(cmake/CMakeRC.cmake)

set(_plantdb_backend_resources_root ${CMAKE_SOURCE_DIR}/resources)

function(_bundle_plantdb_backend_resources _target_name)

    file(GLOB_RECURSE PLANTDB_BACKEND_RESOURCES "${_plantdb_backend_resources_root}/*")
    list(FILTER PLANTDB_BACKEND_RESOURCES EXCLUDE REGEX "\\.DS_Store$")

    cmrc_add_resource_library(
            plantdb_backend_resources 
            ALIAS plantdbbackend::rc
            NAMESPACE plantdb 
            WHENCE ${_plantdb_backend_resources_root}
            ${PLANTDB_BACKEND_RESOURCES}
    )

    target_link_libraries(${_target_name} PUBLIC plantdbbackend::rc)
endfunction()
