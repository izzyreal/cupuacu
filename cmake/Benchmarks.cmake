if(NOT CUPUACU_BUILD_BENCHMARKS)
    if(CUPUACU_BUILD_SDL_BENCHMARKS)
        message(FATAL_ERROR "SDL benchmarks require CUPUACU_BUILD_BENCHMARKS")
    endif()
    return()
endif()

if(CUPUACU_ENABLE_COVERAGE OR CUPUACU_ENABLE_RTSAN_LIBS)
    message(FATAL_ERROR "Use a separate Release build without coverage or sanitizers for benchmarks")
endif()

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
AddDependency(benchmark "benchmark" "${CMAKE_SOURCE_DIR}/deps/benchmark"
    "https://github.com/google/benchmark.git" "v1.9.5")

cupuacu_add_core_target(cupuacu_core_metrics OFF OFF)
target_compile_definitions(cupuacu_core_metrics PUBLIC CUPUACU_WORK_METRICS=1)

# Record resolved revisions, including dependencies tracked on moving branches.
set(_benchmark_dependencies "")
foreach(_dep SDL3 SDL_ttf libsndfile Catch2 readerwriterqueue portaudio benchmark
        spdlog absl alac miniaac json webrtc_apm flac ogg vorbis opus lame mpg123)
    FetchContent_GetProperties(${_dep} SOURCE_DIR _source)
    set(_revision "unavailable")
    if(_source AND EXISTS "${_source}/.git")
        execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${_source}"
            OUTPUT_VARIABLE _revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    string(APPEND _benchmark_dependencies "${_dep}=${_revision};")
endforeach()
configure_file("${CMAKE_CURRENT_LIST_DIR}/BenchmarkBuild.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/BenchmarkBuild.hpp" @ONLY)

add_custom_target(cupuacu_benchmark_metadata
    COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DOUTPUT=${CMAKE_BINARY_DIR}/generated/BenchmarkSourceFingerprint.hpp
        -P ${CMAKE_CURRENT_LIST_DIR}/BenchmarkSourceFingerprint.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/BenchmarkSourceFingerprint.hpp
    VERBATIM)

function(cupuacu_benchmark_target name core use_sdl)
    add_executable(${name} src/benchmark/Benchmarks.cpp)
    target_link_libraries(${name} PRIVATE ${core} benchmark::benchmark)
    target_compile_definitions(${name} PRIVATE CUPUACU_BENCHMARK_SDL=${use_sdl})
    add_dependencies(${name} cupuacu_benchmark_metadata)
    if(WIN32)
        target_link_libraries(${name} PRIVATE psapi)
    endif()
    _bundle_resources(${name})
endfunction()

cupuacu_benchmark_target(cupuacu-benchmarks cupuacu_core 0)
cupuacu_benchmark_target(cupuacu-benchmarks-metrics cupuacu_core_metrics 0)
if(CUPUACU_BUILD_SDL_BENCHMARKS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "SDL benchmarks require the Linux/Xvfb integration environment")
    endif()
    cupuacu_benchmark_target(cupuacu-benchmarks-sdl cupuacu_core 1)
endif()
