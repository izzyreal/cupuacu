include("${CMAKE_CURRENT_LIST_DIR}/WebRtcAudioProcessingSources.cmake")

function(cupuacu_add_webrtc_audio_processing source_root)
    set(webrtc_root "${source_root}/webrtc")
    if(NOT EXISTS "${webrtc_root}/modules/audio_processing/aec3/echo_canceller3.cc")
        message(FATAL_ERROR
            "WebRTC Audio Processing sources not found under ${webrtc_root}")
    endif()

    set(webrtc_apm_sources)
    foreach(relative_source IN LISTS CUPUACU_WEBRTC_AUDIO_PROCESSING_SOURCES)
        set(absolute_source "${webrtc_root}/${relative_source}")
        if(NOT EXISTS "${absolute_source}")
            message(FATAL_ERROR
                "Required WebRTC source is missing: ${relative_source}")
        endif()
        list(APPEND webrtc_apm_sources "${absolute_source}")
    endforeach()
    list(LENGTH webrtc_apm_sources webrtc_apm_source_count)
    message(STATUS
        "WebRTC Audio Processing: compiling ${webrtc_apm_source_count} "
        "AEC3/resampler source files")

    add_library(cupuacu_webrtc_apm STATIC ${webrtc_apm_sources})
    add_library(cupuacu::webrtc_apm ALIAS cupuacu_webrtc_apm)

    set(webrtc_apm_has_x86_slice FALSE)
    if(CMAKE_SYSTEM_PROCESSOR
       MATCHES "^(x86_64|AMD64|amd64|x64|X64|x86|X86|i[3-6]86)$")
        set(webrtc_apm_has_x86_slice TRUE)
    elseif(APPLE
           AND CMAKE_OSX_ARCHITECTURES MATCHES "(^|;)x86_64(;|$)")
        set(webrtc_apm_has_x86_slice TRUE)
    endif()

    if(webrtc_apm_has_x86_slice)
        # A universal Apple build compiles every source for every requested
        # architecture. Generate tiny wrappers so the intrinsic-heavy x86
        # implementations are empty translation units in the arm64 slice.
        set(webrtc_apm_x86_wrapper_dir
            "${CMAKE_CURRENT_BINARY_DIR}/cupuacu_webrtc_x86")
        file(MAKE_DIRECTORY "${webrtc_apm_x86_wrapper_dir}")
        set(webrtc_apm_x86_wrappers)
        foreach(relative_source
                IN LISTS CUPUACU_WEBRTC_AUDIO_PROCESSING_X86_SOURCES)
            set(absolute_source "${webrtc_root}/${relative_source}")
            if(NOT EXISTS "${absolute_source}")
                message(FATAL_ERROR
                    "Required WebRTC x86 source is missing: ${relative_source}")
            endif()

            string(MAKE_C_IDENTIFIER "${relative_source}" wrapper_name)
            set(wrapper_source
                "${webrtc_apm_x86_wrapper_dir}/${wrapper_name}.cc")
            file(GENERATE OUTPUT "${wrapper_source}" CONTENT
                "#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)\n#include \"${absolute_source}\"\n#endif\n")
            list(APPEND webrtc_apm_x86_wrappers "${wrapper_source}")

            if(relative_source MATCHES "_avx2\\.cc$")
                if(MSVC)
                    set_source_files_properties("${wrapper_source}"
                        PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
                else()
                    set_source_files_properties("${wrapper_source}"
                        PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
                endif()
            elseif(relative_source MATCHES "_sse(2)?\\.cc$")
                if(MSVC)
                    # SSE2 is already the x64 baseline; /arch:SSE2 is only a
                    # valid MSVC option for 32-bit x86 targets.
                    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
                        set_source_files_properties("${wrapper_source}"
                            PROPERTIES COMPILE_OPTIONS "/arch:SSE2")
                    endif()
                else()
                    set_source_files_properties("${wrapper_source}"
                        PROPERTIES COMPILE_OPTIONS "-msse2")
                endif()
            endif()
        endforeach()

        target_sources(cupuacu_webrtc_apm PRIVATE
            ${webrtc_apm_x86_wrappers})
    endif()

    target_include_directories(cupuacu_webrtc_apm SYSTEM PUBLIC
        "${webrtc_root}"
    )

    target_compile_definitions(cupuacu_webrtc_apm
        PUBLIC
        WEBRTC_APM_DEBUG_DUMP=0
        WAP_DISABLE_INLINE_SSE
        PRIVATE
        WEBRTC_LIBRARY_IMPL
        PFFFT_SIMD_DISABLE
        _GNU_SOURCE
        _WINSOCKAPI_
        NOMINMAX
    )

    if(APPLE)
        target_compile_definitions(cupuacu_webrtc_apm PUBLIC
            WEBRTC_MAC
            WEBRTC_POSIX
        )
        find_library(webrtc_foundation_framework Foundation REQUIRED)
        target_link_libraries(cupuacu_webrtc_apm PUBLIC
            "${webrtc_foundation_framework}")
    elseif(WIN32)
        target_compile_definitions(cupuacu_webrtc_apm PUBLIC
            WEBRTC_WIN
            _WIN32
            __STDC_FORMAT_MACROS=1
            _USE_MATH_DEFINES
        )
        target_link_libraries(cupuacu_webrtc_apm PUBLIC winmm)
    elseif(UNIX)
        target_compile_definitions(cupuacu_webrtc_apm PUBLIC
            WEBRTC_LINUX
            WEBRTC_POSIX
        )
        find_package(Threads REQUIRED)
        target_link_libraries(cupuacu_webrtc_apm PUBLIC Threads::Threads)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(cupuacu_webrtc_apm PUBLIC rt)
        endif()
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        target_compile_definitions(cupuacu_webrtc_apm PUBLIC WEBRTC_ARCH_ARM64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
        target_compile_definitions(cupuacu_webrtc_apm PUBLIC WEBRTC_ARCH_ARM)
    endif()

    target_link_libraries(cupuacu_webrtc_apm PUBLIC
        absl::base
        absl::bad_optional_access
        absl::flags
        absl::flat_hash_map
        absl::inlined_vector
        absl::strings
        absl::synchronization
    )

    set_target_properties(cupuacu_webrtc_apm PROPERTIES
        C_STANDARD 11
        CXX_STANDARD 20
        POSITION_INDEPENDENT_CODE ON
    )

    if(MSVC)
        target_compile_options(cupuacu_webrtc_apm PRIVATE /W0)
    else()
        target_compile_options(cupuacu_webrtc_apm PRIVATE -w)
    endif()
endfunction()
