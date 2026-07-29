function(cupuacu_add_webrtc_audio_processing source_root)
    set(webrtc_root "${source_root}/webrtc")
    if(NOT EXISTS "${webrtc_root}/modules/audio_processing/aec3/echo_canceller3.cc")
        message(FATAL_ERROR
            "WebRTC Audio Processing sources not found under ${webrtc_root}")
    endif()

    file(GLOB_RECURSE webrtc_apm_sources CONFIGURE_DEPENDS
        "${webrtc_root}/*.c"
        "${webrtc_root}/*.cc"
    )

    # The source snapshot contains optional architecture implementations and
    # one testing helper alongside the portable sources. Keep the portable
    # implementation so the same source set builds on every Cupuacu platform.
    list(FILTER webrtc_apm_sources EXCLUDE REGEX
        "(_avx2|_sse|_sse2|_neon|_mips)\\.(c|cc)$")
    list(FILTER webrtc_apm_sources EXCLUDE REGEX
        "/test_utils\\.cc$")
    list(FILTER webrtc_apm_sources EXCLUDE REGEX
        "/warn_current_thread_is_deadlocked\\.cc$")

    add_library(cupuacu_webrtc_apm STATIC ${webrtc_apm_sources})
    add_library(cupuacu::webrtc_apm ALIAS cupuacu_webrtc_apm)

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
