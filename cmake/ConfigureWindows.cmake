cmake_minimum_required(VERSION 3.19)

if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "ConfigureWindows.cmake requires a Windows host")
endif()

get_filename_component(_source "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED BUILD_DIR)
    set(BUILD_DIR "build/windows-amd64")
endif()
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE BASE_DIR "${_source}")
if(NOT DEFINED FETCHCONTENT_CACHE_ROOT)
    set(FETCHCONTENT_CACHE_ROOT "$ENV{CIWI_FETCHCONTENT_SOURCES_DIR}")
endif()

set(_selection)
if(EXISTS "${BUILD_DIR}/CMakeCache.txt")
    load_cache("${BUILD_DIR}" READ_WITH_PREFIX _cached_
        CMAKE_GENERATOR CMAKE_GENERATOR_PLATFORM CMAKE_GENERATOR_INSTANCE)
    if(NOT _cached_CMAKE_GENERATOR)
        message(FATAL_ERROR "Missing generator in ${BUILD_DIR}/CMakeCache.txt; build directory retained")
    endif()
    list(APPEND _selection -G "${_cached_CMAKE_GENERATOR}")
    if(_cached_CMAKE_GENERATOR_PLATFORM)
        list(APPEND _selection -A "${_cached_CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(_cached_CMAKE_GENERATOR_INSTANCE)
        list(APPEND _selection "-DCMAKE_GENERATOR_INSTANCE=${_cached_CMAKE_GENERATOR_INSTANCE}")
    endif()
    message(STATUS "Reusing ${_cached_CMAKE_GENERATOR} in ${BUILD_DIR}")
else()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E capabilities
        OUTPUT_VARIABLE _capabilities RESULT_VARIABLE _result)
    if(NOT _result STREQUAL "0")
        message(FATAL_ERROR "Cannot query CMake generators: ${_result}")
    endif()
    string(JSON _count LENGTH "${_capabilities}" generators)
    set(_candidates)
    if(_count GREATER 0)
        math(EXPR _last "${_count} - 1")
        foreach(_index RANGE 0 ${_last})
            string(JSON _name GET "${_capabilities}" generators ${_index} name)
            if(_name MATCHES "^Visual Studio ([0-9]+) " AND CMAKE_MATCH_1 GREATER_EQUAL 16)
                list(APPEND _candidates "${CMAKE_MATCH_1}|${_name}")
            endif()
        endforeach()
    endif()
    if(NOT _candidates)
        message(FATAL_ERROR "This CMake does not support a Visual Studio 2019 or newer generator")
    endif()
    list(SORT _candidates COMPARE NATURAL ORDER DESCENDING)

    find_program(_vswhere NAMES vswhere.exe)
    if(NOT _vswhere)
        find_program(_vswhere NAMES vswhere.exe PATHS
            "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/Installer"
            NO_DEFAULT_PATH)
    endif()
    if(NOT _vswhere)
        message(FATAL_ERROR "Cannot locate vswhere.exe; install Visual Studio Installer or add vswhere to PATH")
    endif()
    foreach(_candidate IN LISTS _candidates)
        string(REGEX MATCH "^([0-9]+)\\|(.*)$" _matched "${_candidate}")
        set(_major "${CMAKE_MATCH_1}")
        set(_generator "${CMAKE_MATCH_2}")
        math(EXPR _next "${_major} + 1")
        execute_process(COMMAND "${_vswhere}" -latest -products *
            -version "[${_major}.0,${_next}.0)"
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
            -property installationPath -utf8
            OUTPUT_VARIABLE _instance OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _result ENCODING UTF-8)
        if(NOT _result STREQUAL "0")
            message(FATAL_ERROR "Visual Studio discovery failed: ${_result}")
        endif()
        if(_instance)
            set(_selection -G "${_generator}" -A x64
                "-DCMAKE_GENERATOR_INSTANCE=${_instance}")
            message(STATUS "Selected ${_generator}: ${_instance}")
            break()
        endif()
    endforeach()
    if(NOT _selection)
        message(FATAL_ERROR "No supported Visual Studio installation with x86/x64 C++ tools was found")
    endif()
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${_source}" -B "${BUILD_DIR}"
    ${_selection} -DCUPUACU_BUILD_CCACHE_ENABLED=ON
    "-DFETCHCONTENT_CACHE_ROOT=${FETCHCONTENT_CACHE_ROOT}"
    RESULT_VARIABLE _result)
if(NOT _result STREQUAL "0")
    message(FATAL_ERROR "Cupuacu configuration failed (${_result}); build directory retained")
endif()
