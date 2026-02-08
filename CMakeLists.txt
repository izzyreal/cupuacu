cmake_minimum_required(VERSION 3.14)

include(cmake/ResourceBundling.cmake)
include(cmake/ClangFormat.cmake)

project(Cupuacu)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Prevent FetchContent from re-downloading sources" FORCE)
option(CUPUACU_CODE_SIGNING_ENABLED "Enable post-build code signing on macOS" OFF)
option(CUPUACU_ENABLE_XCODE_TSAN_SUPPRESSIONS
    "Inject TSAN_OPTIONS suppressions into CMake-generated Xcode schemes" ON)
option(CUPUACU_ENABLE_RTSAN_LIBS
    "Enable RTSan standalone runtime via rtsan-libs-cmake" OFF)

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

set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

AddDependency(SDL3 "https://github.com/libsdl-org/SDL.git" "release-3.2.14")
AddDependency(SDL_ttf "https://github.com/libsdl-org/SDL_ttf.git" "release-3.2.2")
AddDependency(libsndfile "https://github.com/libsndfile/libsndfile.git" "master")
AddDependency(Catch2 "https://github.com/catchorg/Catch2.git" "v3.8.1")
AddDependency(readerwriterqueue "https://github.com/cameron314/readerwriterqueue.git" "master")
AddDependency(portaudio "https://github.com/PortAudio/portaudio.git" "master")
AddDependency(PlatformFolders "https://github.com/sago007/PlatformFolders.git" "master")
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)

add_executable(Cupuacu
    src/main/main.cpp
    src/main/State.cpp
    src/main/Paths.cpp
    src/main/persistence/AudioDevicePropertiesPersistence.cpp
    src/main/audio/AudioDevices.cpp
    src/main/audio/AudioCallbackCore.cpp
    src/main/audio/AudioDeviceView.cpp
    src/main/gui/Gui.cpp
    src/main/gui/DocumentSessionWindow.cpp
    src/main/gui/MainView.cpp
    src/main/gui/TriangleMarker.cpp
    src/main/gui/Component.cpp
    src/main/gui/Button.cpp
    src/main/gui/TextButton.cpp
    src/main/gui/Label.cpp
    src/main/gui/DropdownMenu.cpp
    src/main/gui/DevicePropertiesWindow.cpp
    src/main/gui/ScrollBar.cpp
    src/main/gui/WaveformsUnderlay.cpp
    src/main/gui/Waveforms.cpp
    src/main/gui/Waveform.cpp
    src/main/gui/SamplePoint.cpp
    src/main/gui/Menu.cpp
    src/main/gui/MenuBar.cpp
    src/main/gui/Window.cpp
    src/main/actions/Play.cpp
    src/main/actions/Record.cpp
)

add_executable(cupuacu-tests
    src/main/State.cpp
    src/main/Paths.cpp
    src/main/persistence/AudioDevicePropertiesPersistence.cpp
    src/main/audio/AudioDevices.cpp
    src/main/audio/AudioCallbackCore.cpp
    src/main/audio/AudioDeviceView.cpp
    src/main/gui/Gui.cpp
    src/main/gui/DocumentSessionWindow.cpp
    src/main/gui/MainView.cpp
    src/main/gui/TriangleMarker.cpp
    src/main/gui/Component.cpp
    src/main/gui/Button.cpp
    src/main/gui/TextButton.cpp
    src/main/gui/Label.cpp
    src/main/gui/DropdownMenu.cpp
    src/main/gui/DevicePropertiesWindow.cpp
    src/main/gui/ScrollBar.cpp
    src/main/gui/WaveformsUnderlay.cpp
    src/main/gui/Waveforms.cpp
    src/main/gui/Waveform.cpp
    src/main/gui/SamplePoint.cpp
    src/main/gui/Menu.cpp
    src/main/gui/MenuBar.cpp
    src/main/gui/Window.cpp
    src/main/actions/Play.cpp
    src/main/actions/Record.cpp
    src/test/test_playback_follow.cpp
    src/test/test_playback_range.cpp
    src/test/test_edit_commands.cpp
    src/test/test_zoom.cpp
    src/test/test_session_loading.cpp
    src/test/test_component_dirty_rects.cpp
    src/test/test_audio_device_properties_persistence.cpp
    src/test/test_audio_callback_core.cpp
)

if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(cupuacu-tests PRIVATE
        src/test/test_rtsan.cpp
    )
endif()

target_link_libraries(Cupuacu PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)
target_link_libraries(Cupuacu PRIVATE SndFile::sndfile)
target_link_libraries(Cupuacu PRIVATE portaudio)
target_link_libraries(Cupuacu PRIVATE platform_folders)
target_link_libraries(Cupuacu PRIVATE nlohmann_json::nlohmann_json)

target_link_libraries(cupuacu-tests PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)
target_link_libraries(cupuacu-tests PRIVATE SndFile::sndfile)
target_link_libraries(cupuacu-tests PRIVATE portaudio)
target_link_libraries(cupuacu-tests PRIVATE platform_folders)
target_link_libraries(cupuacu-tests PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(cupuacu-tests PRIVATE Catch2::Catch2WithMain)

target_include_directories(Cupuacu PRIVATE src/main/)
target_include_directories(Cupuacu PRIVATE ${readerwriterqueue_SOURCE_DIR})
target_include_directories(cupuacu-tests PRIVATE src/main/)
target_include_directories(cupuacu-tests PRIVATE ${readerwriterqueue_SOURCE_DIR})

_bundle_resources(Cupuacu)
target_link_libraries(cupuacu-tests PUBLIC cupuacu::rc)

set(CUPUACU_RTSAN_SUPPORTED OFF)
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CUPUACU_RTSAN_SUPPORTED ON)
endif()

if(CUPUACU_ENABLE_RTSAN_LIBS OR CUPUACU_RTSAN_SUPPORTED)
    FetchContent_Declare(
        rtsan
        GIT_REPOSITORY https://github.com/izzyreal/rtsan-libs-cmake.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(rtsan)
endif()

if(CUPUACU_ENABLE_RTSAN_LIBS)
    rtsan_libs_enable(TARGET Cupuacu)
    target_compile_definitions(Cupuacu PRIVATE CUPUACU_RTSAN_LIBS_ENABLED=1)
else()
    target_compile_definitions(Cupuacu PRIVATE CUPUACU_RTSAN_LIBS_ENABLED=0)
endif()

if(CUPUACU_RTSAN_SUPPORTED)
    rtsan_libs_enable(TARGET cupuacu-tests)
    target_compile_definitions(cupuacu-tests PRIVATE CUPUACU_RTSAN_LIBS_ENABLED=1)
else()
    target_compile_definitions(cupuacu-tests PRIVATE CUPUACU_RTSAN_LIBS_ENABLED=0)
endif()

if(CMAKE_GENERATOR STREQUAL "Xcode" AND CUPUACU_ENABLE_XCODE_TSAN_SUPPRESSIONS)
    set(_cupuacu_tsan_env "TSAN_OPTIONS=suppressions=\$(SRCROOT)/tsan.supp")
    set_property(TARGET Cupuacu PROPERTY XCODE_GENERATE_SCHEME YES)
    set_property(TARGET cupuacu-tests PROPERTY XCODE_GENERATE_SCHEME YES)
    set_property(TARGET Cupuacu PROPERTY XCODE_SCHEME_ENVIRONMENT "${_cupuacu_tsan_env}")
    set_property(TARGET cupuacu-tests PROPERTY XCODE_SCHEME_ENVIRONMENT "${_cupuacu_tsan_env}")
endif()

if (PROJECT_IS_TOP_LEVEL)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json
    )
endif()

if (APPLE AND CUPUACU_CODE_SIGNING_ENABLED)
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
