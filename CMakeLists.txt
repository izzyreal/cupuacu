cmake_minimum_required(VERSION 3.14)

include(cmake/ResourceBundling.cmake)

project(Cupuacu)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Prevent FetchContent from re-downloading sources" FORCE)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

macro(AddDependency name repo tag)
    FetchContent_Declare(
        ${name}
        GIT_REPOSITORY ${repo}
        GIT_TAG ${tag}
        GIT_SHALLOW TRUE
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/${name}
    )

    FetchContent_MakeAvailable(${name})
endmacro()

set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)

# Dependencies
AddDependency(SDL3 "https://github.com/libsdl-org/SDL.git" "release-3.2.14")
AddDependency(Catch2 "https://github.com/catchorg/Catch2.git" "v3.8.1")

add_executable(Cupuacu
    src/main/main.cpp
)

add_executable(cupuacu-tests
    src/test/test.cpp
)

target_link_libraries(Cupuacu PRIVATE SDL3::SDL3)
target_link_libraries(cupuacu-tests PRIVATE Catch2::Catch2)

if (PROJECT_IS_TOP_LEVEL)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json
    )
endif()

if (APPLE)
    execute_process(
        COMMAND python3 macos-codesign-details-extractor.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE DEVELOPMENT_TEAM OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(CODESIGN_IDENTITY "Apple Development")

    add_custom_command(TARGET Cupuacu
        POST_BUILD
        #COMMAND codesign --force --deep --sign ${CODESIGN_IDENTITY} --timestamp $<TARGET_FILE:Cupuacu>
        COMMAND codesign --force --deep --sign ${CODESIGN_IDENTITY} $<TARGET_FILE:Cupuacu>
        VERBATIM
    )
endif()
