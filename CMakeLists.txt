cmake_minimum_required(VERSION 3.14)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(cmake/ResourceBundling.cmake)
include(cmake/ClangFormat.cmake)
include(cmake/ThreadSanitizer.cmake)
include(cmake/FetchContentCache.cmake)
include(TestBigEndian)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CUPUACU_PROJECT_VERSION_RAW)
string(STRIP "${CUPUACU_PROJECT_VERSION_RAW}" CUPUACU_PROJECT_VERSION)
project(Cupuacu VERSION "${CUPUACU_PROJECT_VERSION}")
if(APPLE)
    enable_language(OBJCXX)
endif()
test_big_endian(CUPUACU_TARGET_BIG_ENDIAN)

option(CUPUACU_BUILD_CCACHE_ENABLED "Enable compiler cache integration via find_package(ccache)" OFF)
if(CUPUACU_BUILD_CCACHE_ENABLED)
    find_package(ccache)
endif()

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Prevent FetchContent from re-downloading sources" FORCE)
if(NOT DEFINED FETCHCONTENT_CACHE_ROOT OR FETCHCONTENT_CACHE_ROOT STREQUAL "")
    if(DEFINED FETCHCONTENT_BASE_DIR AND NOT FETCHCONTENT_BASE_DIR STREQUAL "")
        set(FETCHCONTENT_CACHE_ROOT "${FETCHCONTENT_BASE_DIR}")
    endif()
endif()
if(DEFINED FETCHCONTENT_CACHE_ROOT AND NOT FETCHCONTENT_CACHE_ROOT STREQUAL "")
    file(MAKE_DIRECTORY "${FETCHCONTENT_CACHE_ROOT}")
endif()
option(CUPUACU_CODE_SIGNING_ENABLED "Enable post-build code signing on macOS" OFF)
option(CUPUACU_ENABLE_XCODE_TSAN_SUPPRESSIONS
    "Inject TSAN_OPTIONS suppressions into CMake-generated Xcode schemes" ON)
option(CUPUACU_ENABLE_RTSAN_LIBS
    "Enable RTSan standalone runtime via rtsan-libs-cmake" OFF)
option(CUPUACU_ENABLE_COVERAGE
    "Enable compiler and linker coverage instrumentation for supported targets" OFF)
option(CUPUACU_BUILD_INTEGRATION_TESTS
    "Build SDL/Xvfb-backed integration tests in a dedicated target" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

find_package(Git)

macro(AddDependency name source_name default_source_dir repo tag)
    fetchcontent_declare_cached_git(
        ${name}
        "${source_name}"
        "${default_source_dir}"
        "${repo}"
        "${tag}"
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(${name})
endmacro()

set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_VIDEO ON CACHE BOOL "" FORCE)
set(SDL_RENDER ON CACHE BOOL "" FORCE)
set(SDL_DIALOG ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)

AddDependency(SDL3 "SDL3" "${CMAKE_SOURCE_DIR}/deps/SDL3" "https://github.com/libsdl-org/SDL.git" "release-3.2.14")
AddDependency(SDL_ttf "SDL_ttf" "${CMAKE_SOURCE_DIR}/deps/SDL_ttf" "https://github.com/libsdl-org/SDL_ttf.git" "release-3.2.2")
AddDependency(libsndfile "libsndfile" "${CMAKE_SOURCE_DIR}/deps/libsndfile" "https://github.com/libsndfile/libsndfile.git" "master")
AddDependency(Catch2 "Catch2" "${CMAKE_SOURCE_DIR}/deps/Catch2" "https://github.com/catchorg/Catch2.git" "v3.8.1")
AddDependency(readerwriterqueue "readerwriterqueue" "${CMAKE_SOURCE_DIR}/deps/readerwriterqueue" "https://github.com/cameron314/readerwriterqueue.git" "master")
AddDependency(portaudio "portaudio" "${CMAKE_SOURCE_DIR}/deps/portaudio" "https://github.com/PortAudio/portaudio.git" "master")
AddDependency(PlatformFolders "PlatformFolders" "${CMAKE_SOURCE_DIR}/deps/PlatformFolders" "https://github.com/sago007/PlatformFolders.git" "master")
AddDependency(spdlog "spdlog" "${CMAKE_SOURCE_DIR}/deps/spdlog" "https://github.com/gabime/spdlog.git" "v1.15.3")
fetchcontent_declare_cached_git_source_only(
    alac
    "alac"
    "${CMAKE_SOURCE_DIR}/deps/alac"
    "https://github.com/macosforge/alac.git"
    "master"
)
FetchContent_GetProperties(alac)
if(NOT alac_POPULATED)
    FetchContent_Populate(alac)
endif()
file(GLOB CUPUACU_ALAC_CODEC_SOURCES CONFIGURE_DEPENDS
    "${alac_SOURCE_DIR}/codec/*.c"
    "${alac_SOURCE_DIR}/codec/*.cpp"
)
if(NOT CUPUACU_ALAC_CODEC_SOURCES)
    message(FATAL_ERROR "ALAC codec sources not found under ${alac_SOURCE_DIR}/codec")
endif()
add_library(alac_codec STATIC ${CUPUACU_ALAC_CODEC_SOURCES})
target_include_directories(alac_codec PUBLIC "${alac_SOURCE_DIR}/codec")
set_target_properties(alac_codec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(alac_codec PRIVATE
    copy20ToPredictor=cupuacu_alac_copy20ToPredictor
    copy24ToPredictor=cupuacu_alac_copy24ToPredictor
    copyPredictorTo20=cupuacu_alac_copyPredictorTo20
    copyPredictorTo24=cupuacu_alac_copyPredictorTo24
    copyPredictorTo24Shift=cupuacu_alac_copyPredictorTo24Shift
    copyPredictorTo32=cupuacu_alac_copyPredictorTo32
    copyPredictorTo32Shift=cupuacu_alac_copyPredictorTo32Shift
    mix16=cupuacu_alac_mix16
    mix20=cupuacu_alac_mix20
    mix24=cupuacu_alac_mix24
    mix32=cupuacu_alac_mix32
    unmix16=cupuacu_alac_unmix16
    unmix20=cupuacu_alac_unmix20
    unmix24=cupuacu_alac_unmix24
    unmix32=cupuacu_alac_unmix32
)
if(NOT CUPUACU_TARGET_BIG_ENDIAN)
    target_compile_definitions(alac_codec PUBLIC TARGET_RT_LITTLE_ENDIAN=1)
endif()
target_include_directories(platform_folders INTERFACE
    $<BUILD_INTERFACE:${PlatformFolders_SOURCE_DIR}>
)
fetchcontent_declare_cached_url(
    json
    "json"
    "${CMAKE_SOURCE_DIR}/deps/json"
    "https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

set(CUPUACU_SHARED_SOURCES
    src/main/Document.cpp
    src/main/State.cpp
    src/main/Paths.cpp
    src/main/LongTask.cpp
    src/main/Logger.cpp
    src/main/file/AudioExport.cpp
    src/main/file/AudioFileWriter.cpp
    src/main/file/alac/AlacCodec.cpp
    src/main/file/m4a/M4aAtoms.cpp
    src/main/file/m4a/M4aAlacReader.cpp
    src/main/file/m4a/M4aAlacWriter.cpp
    src/main/file/m4a/M4aParser.cpp
    src/main/persistence/AudioDevicePropertiesPersistence.cpp
    src/main/persistence/DisplayPropertiesPersistence.cpp
    src/main/persistence/DocumentAutosave.cpp
    src/main/persistence/RecentFilesPersistence.cpp
    src/main/persistence/SessionStatePersistence.cpp
    src/main/audio/AudioDevices.cpp
    src/main/audio/AudioCallbackCore.cpp
    src/main/audio/AudioDeviceView.cpp
    src/main/gui/Gui.cpp
    src/main/gui/DocumentSessionWindow.cpp
    src/main/gui/MainView.cpp
    src/main/gui/TriangleMarker.cpp
    src/main/gui/DocumentMarkerHandle.cpp
    src/main/gui/Component.cpp
    src/main/gui/Button.cpp
    src/main/gui/TextButton.cpp
    src/main/gui/Label.cpp
    src/main/gui/DropdownMenu.cpp
    src/main/gui/DevicePropertiesWindow.cpp
    src/main/gui/DisplaySettingsWindow.cpp
    src/main/gui/OptionsWindow.cpp
    src/main/gui/NewFileDialogWindow.cpp
    src/main/gui/GenerateSilenceDialogWindow.cpp
    src/main/gui/ExportAudioDialogWindow.cpp
    src/main/gui/MarkerEditorDialogWindow.cpp
    src/main/effects/AmplifyEnvelopeEffect.cpp
    src/main/effects/AmplifyFadeEffect.cpp
    src/main/effects/DynamicsEffect.cpp
    src/main/effects/RemoveSilenceEffect.cpp
    src/main/gui/ScrollBar.cpp
    src/main/gui/Slider.cpp
    src/main/gui/TextInput.cpp
    src/main/gui/WaveformsUnderlay.cpp
    src/main/gui/Waveforms.cpp
    src/main/gui/Waveform.cpp
    src/main/gui/ControlPointHandle.cpp
    src/main/gui/SamplePoint.cpp
    src/main/gui/Menu.cpp
    src/main/gui/MenuBar.cpp
    src/main/gui/TooltipController.cpp
    src/main/gui/Window.cpp
    src/main/actions/effects/BackgroundEffect.cpp
    src/main/actions/io/BackgroundOpen.cpp
    src/main/actions/io/BackgroundSave.cpp
    src/main/actions/Play.cpp
    src/main/actions/Record.cpp
)

if(APPLE)
    list(APPEND CUPUACU_SHARED_SOURCES
        src/main/actions/CocoaSaveFileDialog.mm
    )
endif()

set(CUPUACU_TEST_SOURCES
    src/test/TestSdlTtfGuard.cpp
    src/test/test_playback_range.cpp
    src/test/test_zoom.cpp
    src/test/test_edit_commands.cpp
    src/test/test_waveform_selection.cpp
    src/test/test_session_loading.cpp
    src/test/test_aiff_writer.cpp
    src/test/test_wav_writer.cpp
    src/test/test_alac_codec.cpp
    src/test/test_m4a_atoms.cpp
    src/test/test_m4a_parser.cpp
    src/test/test_m4a_writer.cpp
    src/test/test_overwrite_preservation_mutation.cpp
    src/test/test_audio_device_properties_persistence.cpp
    src/test/test_display_properties_persistence.cpp
    src/test/test_document_autosave.cpp
    src/test/test_recent_files_persistence.cpp
    src/test/test_document_tabs.cpp
    src/test/test_document_markers.cpp
    src/test/test_marker_persistence.cpp
    src/test/test_sample_quantization.cpp
    src/test/test_audio_callback_core.cpp
    src/test/test_long_task.cpp
    src/test/test_background_effects.cpp
    src/test/test_action_undoables.cpp
    src/test/test_amplify_envelope_effect.cpp
    src/test/test_audio_devices_orchestration.cpp
    src/test/test_menu_bar_labels.cpp
    src/test/test_window_mouse_routing.cpp
    src/test/test_gui_primitives.cpp
    src/test/test_render_helpers.cpp
    src/test/test_main_view_layout_planning.cpp
    src/test/test_main_view_selection_marker_planning.cpp
    src/test/test_waveforms_underlay_planning.cpp
    src/test/test_component_planning.cpp
    src/test/test_wheel_scroll_planning.cpp
    src/test/test_timeline_planning.cpp
    src/test/test_menu_planning.cpp
    src/test/test_waveform_interaction_planning.cpp
    src/test/test_transport_and_vumeter.cpp
    src/test/test_remove_silence_effect.cpp
    src/test/test_tooltip_planning.cpp
    src/test/test_waveform_render_and_buffers.cpp
)

set(CUPUACU_INTEGRATION_TEST_SOURCES
    src/test/TestSdlTtfGuard.cpp
    src/test/integration/IntegrationTestHelpers.hpp
    src/test/integration/test_runtime_contract.cpp
    src/test/integration/test_empty_session.cpp
    src/test/integration/test_main_view_structure.cpp
    src/test/integration/test_event_and_waveform_runtime.cpp
    src/test/integration/test_open_file_dialog_callback.cpp
    src/test/integration/test_window_rendering.cpp
    src/test/integration/test_window_menu_behavior.cpp
    src/test/integration/test_menu_bar_effects.cpp
    src/test/integration/test_playback_follow.cpp
    src/test/integration/test_main_view_recording.cpp
)

set(CUPUACU_RTSAN_SUPPORTED OFF)
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CUPUACU_RTSAN_SUPPORTED ON)
endif()

set(CUPUACU_TSAN_SUPPORTED OFF)
if(APPLE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(CUPUACU_TSAN_SUPPORTED ON)
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CUPUACU_TSAN_SUPPORTED ON)
    endif()
endif()

if(CUPUACU_ENABLE_RTSAN_LIBS OR CUPUACU_RTSAN_SUPPORTED)
    fetchcontent_declare_cached_git(
        rtsan
        "rtsan"
        "${CMAKE_SOURCE_DIR}/deps/rtsan"
        "https://github.com/izzyreal/rtsan-libs-cmake.git"
        "main"
    )
    FetchContent_MakeAvailable(rtsan)
endif()

add_library(cupuacu_deps INTERFACE)
target_link_libraries(cupuacu_deps INTERFACE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    SndFile::sndfile
    portaudio
    alac_codec
    platform_folders
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

function(cupuacu_enable_coverage_for_target target)
    if(NOT CUPUACU_ENABLE_COVERAGE)
        return()
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options("${target}" PRIVATE -O0 -g --coverage)
        target_link_options("${target}" PRIVATE --coverage)
    else()
        message(WARNING "Coverage requested for unsupported compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()

function(cupuacu_add_core_target target enable_rtsan enable_tsan)
    add_library("${target}" STATIC ${CUPUACU_SHARED_SOURCES})
    target_link_libraries("${target}" PUBLIC cupuacu_deps)
    target_include_directories("${target}" PUBLIC
        src/main/
        ${readerwriterqueue_SOURCE_DIR}
        ${CMRC_INCLUDE_DIR}
    )

    if(enable_tsan)
        cupuacu_enable_tsan_for_target("${target}" _tsan_enabled_for_core)
        if(NOT _tsan_enabled_for_core)
            message(FATAL_ERROR "ThreadSanitizer not active for core target '${target}'")
        endif()
        target_compile_definitions("${target}" PUBLIC
            CUPUACU_RTSAN_LIBS_ENABLED=0
            CUPUACU_TSAN_ENABLED=1
        )
    elseif(enable_rtsan)
        if(NOT CUPUACU_RTSAN_SUPPORTED)
            message(FATAL_ERROR "RTSan requested for unsupported core target '${target}'")
        endif()
        rtsan_libs_enable(TARGET "${target}")
        target_compile_definitions("${target}" PUBLIC
            CUPUACU_RTSAN_LIBS_ENABLED=1
            CUPUACU_TSAN_ENABLED=0
        )
    else()
        target_compile_definitions("${target}" PUBLIC
            CUPUACU_RTSAN_LIBS_ENABLED=0
            CUPUACU_TSAN_ENABLED=0
        )
    endif()
    cupuacu_enable_coverage_for_target("${target}")
endfunction()

cupuacu_add_core_target(cupuacu_core OFF OFF)

if(CUPUACU_RTSAN_SUPPORTED)
    cupuacu_add_core_target(cupuacu_core_rtsan ON OFF)
endif()

if(CUPUACU_TSAN_SUPPORTED)
    cupuacu_add_core_target(cupuacu_core_tsan OFF ON)
endif()

set(_cupuacu_app_core_target cupuacu_core)
if(CUPUACU_ENABLE_RTSAN_LIBS)
    if(CUPUACU_RTSAN_SUPPORTED)
        set(_cupuacu_app_core_target cupuacu_core_rtsan)
    else()
        message(FATAL_ERROR "CUPUACU_ENABLE_RTSAN_LIBS=ON but RTSan is unsupported for this platform")
    endif()
endif()

if(APPLE)
    add_executable(Cupuacu MACOSX_BUNDLE
        src/main/main.cpp
    )
else()
    add_executable(Cupuacu
        src/main/main.cpp
    )
endif()

set(CUPUACU_ICON_DIR "${CMAKE_SOURCE_DIR}/resources/app-icons")
set(CUPUACU_MAC_ICONSET_DIR
    "${CUPUACU_ICON_DIR}/macos/Cupuacu.iconset")
set(CUPUACU_MAC_ICNS "${CUPUACU_ICON_DIR}/macos/Cupuacu.icns")
set(CUPUACU_WINDOWS_ICON "${CUPUACU_ICON_DIR}/windows/cupuacu.ico")
set(CUPUACU_WINDOWS_MANIFEST "${CMAKE_SOURCE_DIR}/cmake/Cupuacu.exe.manifest")
set(CUPUACU_LINUX_ICON "${CUPUACU_ICON_DIR}/linux/cupuacu.png")

if(APPLE)
    set_source_files_properties("${CUPUACU_MAC_ICNS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(Cupuacu PRIVATE "${CUPUACU_MAC_ICNS}")
    set_target_properties(Cupuacu PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "Cupuacu.icns"
        MACOSX_BUNDLE_BUNDLE_NAME "Cupuacu"
        MACOSX_BUNDLE_GUI_IDENTIFIER "nl.izmar.cupuacu"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()

if(WIN32)
    configure_file(cmake/Cupuacu.rc.in "${CMAKE_BINARY_DIR}/Cupuacu.rc" @ONLY)
    target_sources(Cupuacu PRIVATE "${CMAKE_BINARY_DIR}/Cupuacu.rc")
endif()

if(UNIX AND NOT APPLE)
    configure_file(cmake/Cupuacu.desktop.in
        "${CMAKE_BINARY_DIR}/Cupuacu.desktop" @ONLY)
    install(FILES "${CMAKE_BINARY_DIR}/Cupuacu.desktop"
        DESTINATION share/applications)
    install(FILES "${CUPUACU_LINUX_ICON}"
        DESTINATION share/icons/hicolor/256x256/apps)
endif()
if(APPLE)
    target_sources(Cupuacu PRIVATE
        src/main/platform/macos/MenuAdjustments.mm
    )
endif()
target_link_libraries(Cupuacu PRIVATE "${_cupuacu_app_core_target}")
if(APPLE)
    find_library(CUPUACU_APPKIT_FRAMEWORK AppKit REQUIRED)
    target_link_libraries(Cupuacu PRIVATE "${CUPUACU_APPKIT_FRAMEWORK}")
endif()
if(CUPUACU_ENABLE_RTSAN_LIBS)
    rtsan_libs_enable(TARGET Cupuacu)
endif()

add_executable(cupuacu-tests
    ${CUPUACU_TEST_SOURCES}
)
target_link_libraries(cupuacu-tests PRIVATE cupuacu_core Catch2::Catch2WithMain)
target_compile_definitions(cupuacu-tests PRIVATE
    CUPUACU_RTSAN_LIBS_ENABLED=0
    CUPUACU_TSAN_ENABLED=0
)
cupuacu_enable_coverage_for_target(cupuacu-tests)

_bundle_resources(Cupuacu)
target_link_libraries(cupuacu-tests PUBLIC cupuacu::rc)
_bundle_test_resources(cupuacu-tests)

if(CUPUACU_BUILD_INTEGRATION_TESTS)
    add_executable(cupuacu-tests-integration
        ${CUPUACU_INTEGRATION_TEST_SOURCES}
    )
    target_link_libraries(cupuacu-tests-integration PRIVATE cupuacu_core
        Catch2::Catch2WithMain)
    target_compile_definitions(cupuacu-tests-integration PRIVATE
        CUPUACU_RTSAN_LIBS_ENABLED=0
        CUPUACU_TSAN_ENABLED=0
    )
    cupuacu_enable_coverage_for_target(cupuacu-tests-integration)
    target_link_libraries(cupuacu-tests-integration PUBLIC cupuacu::rc)
    _bundle_test_resources(cupuacu-tests-integration)
endif()

if(CUPUACU_RTSAN_SUPPORTED)
    add_executable(cupuacu-tests-rtsan
        src/test/test_rtsan.cpp
        src/test/test_audio_callback_core.cpp
    )
    target_link_libraries(cupuacu-tests-rtsan PRIVATE cupuacu_core_rtsan Catch2::Catch2WithMain)
    target_link_libraries(cupuacu-tests-rtsan PUBLIC cupuacu::rc)
    _bundle_test_resources(cupuacu-tests-rtsan)
    rtsan_libs_enable(TARGET cupuacu-tests-rtsan)
    target_compile_definitions(cupuacu-tests-rtsan PRIVATE
        CUPUACU_RTSAN_LIBS_ENABLED=1
        CUPUACU_TSAN_ENABLED=0
    )

    if(CUPUACU_TSAN_SUPPORTED)
        add_executable(cupuacu-tsan-race-probe
            src/test/tsan_race_probe.cpp
        )
        cupuacu_enable_tsan_for_target(cupuacu-tsan-race-probe CUPUACU_TSAN_ENABLED_FOR_PROBE)
        if(NOT CUPUACU_TSAN_ENABLED_FOR_PROBE)
            message(FATAL_ERROR "cupuacu-tsan-race-probe created but TSan is inactive")
        endif()

        add_executable(cupuacu-tests-tsan
            ${CUPUACU_TEST_SOURCES}
            src/test/test_tsan_verification.cpp
        )
        target_link_libraries(cupuacu-tests-tsan PRIVATE cupuacu_core_tsan Catch2::Catch2WithMain)
        target_link_libraries(cupuacu-tests-tsan PUBLIC cupuacu::rc)
        _bundle_test_resources(cupuacu-tests-tsan)
        target_compile_definitions(cupuacu-tests-tsan PRIVATE CUPUACU_RTSAN_LIBS_ENABLED=0)
        target_compile_definitions(cupuacu-tests-tsan PRIVATE
            CUPUACU_TSAN_RACE_PROBE_PATH="$<TARGET_FILE:cupuacu-tsan-race-probe>")
        add_dependencies(cupuacu-tests-tsan cupuacu-tsan-race-probe)
        cupuacu_enable_tsan_for_target(cupuacu-tests-tsan CUPUACU_TSAN_ENABLED_FOR_TESTS)
        if(CUPUACU_TSAN_ENABLED_FOR_TESTS)
            message(STATUS "ThreadSanitizer enabled for cupuacu-tests-tsan")
        endif()
    else()
        message(STATUS "ThreadSanitizer not supported for this compiler/platform; cupuacu-tests-tsan not created")
    endif()
endif()

if(CMAKE_GENERATOR STREQUAL "Xcode" AND CUPUACU_ENABLE_XCODE_TSAN_SUPPRESSIONS)
    set(_cupuacu_tsan_env "TSAN_OPTIONS=suppressions=\$(SRCROOT)/tsan.supp")
    set_property(TARGET Cupuacu PROPERTY XCODE_GENERATE_SCHEME YES)
    set_property(TARGET cupuacu-tests PROPERTY XCODE_GENERATE_SCHEME YES)
    set_property(TARGET Cupuacu PROPERTY XCODE_SCHEME_ENVIRONMENT "${_cupuacu_tsan_env}")
    set_property(TARGET cupuacu-tests PROPERTY XCODE_SCHEME_ENVIRONMENT "${_cupuacu_tsan_env}")
    if(TARGET cupuacu-tests-tsan)
        set_property(TARGET cupuacu-tests-tsan PROPERTY XCODE_GENERATE_SCHEME YES)
        set_property(TARGET cupuacu-tests-tsan PROPERTY XCODE_SCHEME_ENVIRONMENT "${_cupuacu_tsan_env}")
    endif()
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
