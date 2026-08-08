set(CUPUACU_GENERATED_RESOURCES_ROOT
    "${CMAKE_BINARY_DIR}/generated-resources")
file(MAKE_DIRECTORY "${CUPUACU_GENERATED_RESOURCES_ROOT}")

set(CUPUACU_THIRD_PARTY_CREDITS
    "${CUPUACU_GENERATED_RESOURCES_ROOT}/THIRD_PARTY_CREDITS.txt")
set(CUPUACU_THIRD_PARTY_NOTICES
    "${CUPUACU_GENERATED_RESOURCES_ROOT}/THIRD_PARTY_NOTICES.txt")

function(cupuacu_dependency_revision source_dir fallback output_variable)
    set(_revision "${fallback}")
    if(Git_FOUND AND EXISTS "${source_dir}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
            WORKING_DIRECTORY "${source_dir}"
            OUTPUT_VARIABLE _git_revision
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(NOT _git_revision STREQUAL "")
            set(_revision "${fallback} (${_git_revision})")
        endif()
    endif()
    set("${output_variable}" "${_revision}" PARENT_SCOPE)
endfunction()

cupuacu_dependency_revision("${portaudio_SOURCE_DIR}" "19.8"
    CUPUACU_PORTAUDIO_VERSION)
cupuacu_dependency_revision("${libsndfile_SOURCE_DIR}" "1.2.2"
    CUPUACU_LIBSNDFILE_VERSION)
cupuacu_dependency_revision("${alac_SOURCE_DIR}" "source revision"
    CUPUACU_ALAC_VERSION)
cupuacu_dependency_revision("${readerwriterqueue_SOURCE_DIR}" "1.0.7"
    CUPUACU_READERWRITERQUEUE_VERSION)
cupuacu_dependency_revision("${platform_folders_SOURCE_DIR}" "4.2.0"
    CUPUACU_PLATFORMFOLDERS_VERSION)

file(WRITE "${CUPUACU_THIRD_PARTY_CREDITS}"
"CREDITS\n\nCupuacu is made possible by these open-source projects.\n\n")
file(WRITE "${CUPUACU_THIRD_PARTY_NOTICES}"
"Cupuacu third-party notices\n"
"============================\n\n"
"The following notices are reproduced from the source distributions used "
"to build Cupuacu.\n\n")

function(cupuacu_add_third_party name version purpose license homepage
         license_url license_file)
    file(APPEND "${CUPUACU_THIRD_PARTY_CREDITS}"
        "${name}\n"
        "${homepage}\n"
        "${purpose}.\n"
        "Version: ${version}\n"
        "License: ${license}\n"
        "${license_url}\n\n")

    if(EXISTS "${license_file}")
        file(READ "${license_file}" _license_text)
        file(APPEND "${CUPUACU_THIRD_PARTY_NOTICES}"
            "-------------------------------------------------------------------------------\n"
            "${name} (${version})\n"
            "${homepage}\n"
            "-------------------------------------------------------------------------------\n\n"
            "${_license_text}\n\n")
    else()
        message(WARNING
            "License file for ${name} was not found at ${license_file}")
    endif()
endfunction()

cupuacu_add_third_party(
    "SDL" "3.2.14" "Windows, input, rendering, dialogs and platform integration"
    "Zlib" "https://www.libsdl.org/"
    "https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt"
    "${SDL3_SOURCE_DIR}/LICENSE.txt")
cupuacu_add_third_party(
    "SDL_ttf" "3.2.2" "Font loading and text rendering"
    "Zlib" "https://github.com/libsdl-org/SDL_ttf"
    "https://github.com/libsdl-org/SDL_ttf/blob/main/LICENSE.txt"
    "${SDL3_ttf_SOURCE_DIR}/LICENSE.txt")
cupuacu_add_third_party(
    "FreeType" "bundled with SDL_ttf" "Font rasterization"
    "FreeType License" "https://freetype.org/"
    "https://gitlab.freedesktop.org/freetype/freetype/-/blob/master/docs/FTL.TXT"
    "${freetype_SOURCE_DIR}/docs/FTL.TXT")
cupuacu_add_third_party(
    "HarfBuzz" "bundled with SDL_ttf" "Unicode text shaping"
    "Old MIT" "https://harfbuzz.github.io/"
    "https://github.com/harfbuzz/harfbuzz/blob/main/COPYING"
    "${harfbuzz_SOURCE_DIR}/COPYING")
cupuacu_add_third_party(
    "Inter" "embedded application font" "Cupuacu's user-interface typeface"
    "OFL-1.1" "https://rsms.me/inter/"
    "https://github.com/rsms/inter/blob/master/LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/resources/licenses/Inter-OFL.txt")
cupuacu_add_third_party(
    "PortAudio" "${CUPUACU_PORTAUDIO_VERSION}" "Cross-platform audio input and output"
    "MIT" "https://www.portaudio.com/"
    "https://github.com/PortAudio/portaudio/blob/master/LICENSE.txt"
    "${portaudio_SOURCE_DIR}/LICENSE.txt")
cupuacu_add_third_party(
    "libsndfile" "${CUPUACU_LIBSNDFILE_VERSION}" "Audio-file reading and writing"
    "LGPL-2.1-or-later" "https://libsndfile.github.io/libsndfile/"
    "https://github.com/libsndfile/libsndfile/blob/master/COPYING"
    "${libsndfile_SOURCE_DIR}/COPYING")
cupuacu_add_third_party(
    "Apple Lossless Audio Codec" "${CUPUACU_ALAC_VERSION}" "ALAC encoding and decoding"
    "Apache-2.0" "https://github.com/macosforge/alac"
    "https://github.com/macosforge/alac/blob/master/LICENSE"
    "${alac_SOURCE_DIR}/LICENSE")
cupuacu_add_third_party(
    "Miniaac" "1.0.0" "AAC-LC decoding"
    "0BSD" "https://buffering.party/software/miniaac/"
    "https://buffering.party/software/miniaac/"
    "${miniaac_SOURCE_DIR}/LICENSE")
cupuacu_add_third_party(
    "WebRTC Audio Processing" "846fe90a289f" "Feedback suppression"
    "BSD-3-Clause" "https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing"
    "https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing/-/blob/master/webrtc/LICENSE"
    "${webrtc_apm_SOURCE_DIR}/webrtc/LICENSE")
cupuacu_add_third_party(
    "Abseil" "20240722.0" "Foundational C++ utilities used by audio processing"
    "Apache-2.0" "https://abseil.io/"
    "https://github.com/abseil/abseil-cpp/blob/master/LICENSE"
    "${absl_SOURCE_DIR}/LICENSE")
cupuacu_add_third_party(
    "spdlog" "1.15.3" "Application logging"
    "MIT" "https://github.com/gabime/spdlog"
    "https://github.com/gabime/spdlog/blob/v1.x/LICENSE"
    "${spdlog_SOURCE_DIR}/LICENSE")
cupuacu_add_third_party(
    "fmt" "bundled with spdlog" "Text formatting"
    "MIT" "https://fmt.dev/"
    "https://github.com/fmtlib/fmt/blob/master/LICENSE"
    "${spdlog_SOURCE_DIR}/include/spdlog/fmt/bundled/fmt.license.rst")
cupuacu_add_third_party(
    "nlohmann/json" "3.12.0" "JSON parsing and serialization"
    "MIT" "https://json.nlohmann.me/"
    "https://github.com/nlohmann/json/blob/develop/LICENSE.MIT"
    "${json_SOURCE_DIR}/LICENSE.MIT")
cupuacu_add_third_party(
    "readerwriterqueue" "${CUPUACU_READERWRITERQUEUE_VERSION}" "Lock-free realtime message queues"
    "Simplified BSD" "https://github.com/cameron314/readerwriterqueue"
    "https://github.com/cameron314/readerwriterqueue/blob/master/LICENSE.md"
    "${readerwriterqueue_SOURCE_DIR}/LICENSE.md")
cupuacu_add_third_party(
    "PlatformFolders" "${CUPUACU_PLATFORMFOLDERS_VERSION}" "Platform-specific application data paths"
    "MIT" "https://github.com/sago007/PlatformFolders"
    "https://github.com/sago007/PlatformFolders/blob/master/LICENSE"
    "${platform_folders_SOURCE_DIR}/LICENSE")

file(APPEND "${CUPUACU_THIRD_PARTY_CREDITS}"
    "DEVELOPMENT AND TESTING\n\n"
    "CMake\n"
    "https://cmake.org/\n"
    "C/C++ build configuration.\n"
    "License: BSD-3-Clause\n"
    "https://github.com/Kitware/CMake/blob/master/Copyright.txt\n\n"
    "Catch2\n"
    "https://github.com/catchorg/Catch2\n"
    "Unit and integration testing.\n"
    "Version: 3.8.1\n"
    "License: BSL-1.0\n"
    "https://github.com/catchorg/Catch2/blob/devel/LICENSE.txt\n\n"
    "RTSan and ThreadSanitizer\n"
    "https://clang.llvm.org/docs/RealtimeSanitizer.html\n"
    "Realtime-safety and concurrency testing.\n"
    "License: Apache-2.0 WITH LLVM-exception\n"
    "https://github.com/llvm/llvm-project/blob/main/LICENSE.TXT\n")

set(CUPUACU_GENERATED_RESOURCE_FILES
    "${CUPUACU_THIRD_PARTY_CREDITS}"
    "${CUPUACU_THIRD_PARTY_NOTICES}")
