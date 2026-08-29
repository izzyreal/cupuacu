include(FetchContent)

set(CUPUACU_AUDIO_CODEC_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(cupuacu_exclude_dependency source_dir)
    if(source_dir)
        set_property(DIRECTORY "${source_dir}" PROPERTY EXCLUDE_FROM_ALL TRUE)
    endif()
endfunction()

function(cupuacu_add_lame source_dir)
    set(_lame_sources
        VbrTag.c
        bitstream.c
        encoder.c
        fft.c
        gain_analysis.c
        id3tag.c
        lame.c
        newmdct.c
        presets.c
        psymodel.c
        quantize.c
        quantize_pvt.c
        reservoir.c
        set_get.c
        tables.c
        takehiro.c
        util.c
        vbrquantize.c
        version.c)
    list(TRANSFORM _lame_sources PREPEND "${source_dir}/libmp3lame/")

    configure_file(
        "${CUPUACU_AUDIO_CODEC_CMAKE_DIR}/LameConfig.h.in"
        "${CMAKE_BINARY_DIR}/generated/lame/config.h" @ONLY)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/lame/include/lame")
    configure_file(
        "${source_dir}/include/lame.h"
        "${CMAKE_BINARY_DIR}/generated/lame/include/lame/lame.h" COPYONLY)

    add_library(cupuacu_lame STATIC ${_lame_sources})
    add_library(mp3lame::mp3lame ALIAS cupuacu_lame)
    target_compile_definitions(cupuacu_lame PRIVATE HAVE_CONFIG_H)
    target_include_directories(cupuacu_lame
        PUBLIC "${CMAKE_BINARY_DIR}/generated/lame/include"
        PRIVATE
            "${source_dir}/include"
            "${source_dir}/libmp3lame"
            "${CMAKE_BINARY_DIR}/generated/lame")
    if(NOT MSVC)
        target_link_libraries(cupuacu_lame PUBLIC m)
    endif()
    set_target_properties(cupuacu_lame PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED TRUE
        POSITION_INDEPENDENT_CODE TRUE)
endfunction()

function(cupuacu_add_audio_codec_dependencies)
    # Vorbis 1.3.7 predates CMake 4's removal of policy compatibility below
    # 3.5, but its build remains compatible with current CMake releases.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    # Several upstream projects use the same generic install export-set name.
    # Cupuacu embeds their static libraries and does not install their targets.
    set(CMAKE_SKIP_INSTALL_RULES ON)
    # Use exact upstream revisions so release capabilities do not depend on
    # packages installed on the build host.
    fetchcontent_declare_cached_git(
        ogg "ogg" "${CMAKE_SOURCE_DIR}/deps/ogg"
        "https://github.com/xiph/ogg.git"
        "be05b13e98b048f0b5a0f5fa8ce514d56db5f822")
    set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(INSTALL_CMAKE_PACKAGE_MODULE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(ogg)
    cupuacu_exclude_dependency("${ogg_SOURCE_DIR}")
    set(OGG_INCLUDE_DIR "${ogg_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(OGG_LIBRARY ogg CACHE STRING "" FORCE)

    fetchcontent_declare_cached_git(
        vorbis "vorbis" "${CMAKE_SOURCE_DIR}/deps/vorbis"
        "https://github.com/xiph/vorbis.git"
        "0657aee69dec8508a0011f47f3b69d7538e9d262")
    FetchContent_MakeAvailable(vorbis)
    cupuacu_exclude_dependency("${vorbis_SOURCE_DIR}")
    add_library(Vorbis::vorbis ALIAS vorbis)
    add_library(Vorbis::vorbisenc ALIAS vorbisenc)
    add_library(Vorbis::vorbisfile ALIAS vorbisfile)
    set(Vorbis_Vorbis_INCLUDE_DIR
        "${vorbis_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(Vorbis_Enc_INCLUDE_DIR
        "${vorbis_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(Vorbis_File_INCLUDE_DIR
        "${vorbis_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(Vorbis_Vorbis_LIBRARY vorbis CACHE STRING "" FORCE)
    set(Vorbis_Enc_LIBRARY vorbisenc CACHE STRING "" FORCE)
    set(Vorbis_File_LIBRARY vorbisfile CACHE STRING "" FORCE)

    fetchcontent_declare_cached_git(
        flac "flac" "${CMAKE_SOURCE_DIR}/deps/flac"
        "https://github.com/xiph/flac.git"
        "1507800de4b70e21be71f38caa0d9079d0bc6e45")
    set(BUILD_CXXLIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(WITH_OGG OFF CACHE BOOL "" FORCE)
    set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
    set(INSTALL_PKGCONFIG_MODULES OFF CACHE BOOL "" FORCE)
    set(INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(WITH_ASM OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(flac)
    cupuacu_exclude_dependency("${flac_SOURCE_DIR}")
    set(FLAC_INCLUDE_DIR "${flac_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(FLAC_LIBRARY FLAC CACHE STRING "" FORCE)

    fetchcontent_declare_cached_git(
        opus "opus" "${CMAKE_SOURCE_DIR}/deps/opus"
        "https://github.com/xiph/opus.git"
        "ddbe48383984d56acd9e1ab6a090c54ca6b735a6")
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_DISABLE_INTRINSICS ON CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_DRED OFF CACHE BOOL "" FORCE)
    set(OPUS_OSCE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(opus)
    cupuacu_exclude_dependency("${opus_SOURCE_DIR}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/opus/include/opus")
    foreach(_opus_header
            opus.h opus_custom.h opus_defines.h opus_multistream.h
            opus_projection.h opus_types.h)
        configure_file(
            "${opus_SOURCE_DIR}/include/${_opus_header}"
            "${CMAKE_BINARY_DIR}/generated/opus/include/opus/${_opus_header}"
            COPYONLY)
    endforeach()
    target_include_directories(opus PUBLIC
        "${CMAKE_BINARY_DIR}/generated/opus/include")
    set(OPUS_INCLUDE_DIR
        "${CMAKE_BINARY_DIR}/generated/opus/include" CACHE PATH "" FORCE)
    set(OPUS_LIBRARY opus CACHE STRING "" FORCE)

    fetchcontent_declare_cached_url(
        mpg123 "mpg123" "${CMAKE_SOURCE_DIR}/deps/mpg123"
        "https://www.mpg123.de/download/mpg123-1.33.7.tar.bz2"
        URL_HASH
        "SHA256=31d0e35a4ca567ec9b5ebda6c3062bb4435d6d3eacd6ef0d95cadd7854dc03ee"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(BUILD_LIBOUT123 OFF CACHE BOOL "" FORCE)
    set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    # libsndfile uses mpg123's regular file/seek API. PORTABLE_API removes
    # those entry points even though the declarations remain visible.
    set(PORTABLE_API OFF CACHE BOOL "" FORCE)
    # Architecture-specific assembly cannot be compiled into a single Apple
    # universal target. Use the generic decoder everywhere so the dependency
    # has one deterministic, portable configuration on every build host.
    foreach(_arch X86 X64 ARM32 ARM64)
        set(HAVE_ARCH_IS_${_arch} FALSE CACHE INTERNAL "" FORCE)
    endforeach()
    FetchContent_GetProperties(mpg123)
    if(NOT mpg123_POPULATED)
        FetchContent_Populate(mpg123)
    endif()
    add_subdirectory(
        "${mpg123_SOURCE_DIR}/ports/cmake" "${mpg123_BINARY_DIR}")
    cupuacu_exclude_dependency("${mpg123_SOURCE_DIR}/ports/cmake")
    add_library(MPG123::libmpg123 ALIAS libmpg123)
    target_include_directories(libmpg123 PUBLIC
        "${mpg123_SOURCE_DIR}/src/include")
    set(mpg123_INCLUDE_DIR
        "${mpg123_SOURCE_DIR}/src/include" CACHE PATH "" FORCE)
    set(mpg123_LIBRARY libmpg123 CACHE STRING "" FORCE)

    fetchcontent_declare_cached_url(
        lame "lame" "${CMAKE_SOURCE_DIR}/deps/lame"
        "https://downloads.sourceforge.net/project/lame/lame/4.0/lame-4.0.tar.gz"
        URL_HASH
        "SHA256=3df5124d5ad3a98312ffd7ba6a9b36230e4f8a3e66d3ce0f425e336c32d216eb"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(lame)
    cupuacu_add_lame("${lame_SOURCE_DIR}")
    set(MP3LAME_INCLUDE_DIR
        "${CMAKE_BINARY_DIR}/generated/lame/include" CACHE PATH "" FORCE)
    set(MP3LAME_LIBRARY cupuacu_lame CACHE STRING "" FORCE)

    set(CUPUACU_OGG_SOURCE_DIR "${ogg_SOURCE_DIR}" PARENT_SCOPE)
    set(CUPUACU_VORBIS_SOURCE_DIR "${vorbis_SOURCE_DIR}" PARENT_SCOPE)
    set(CUPUACU_FLAC_SOURCE_DIR "${flac_SOURCE_DIR}" PARENT_SCOPE)
    set(CUPUACU_OPUS_SOURCE_DIR "${opus_SOURCE_DIR}" PARENT_SCOPE)
    set(CUPUACU_MPG123_SOURCE_DIR "${mpg123_SOURCE_DIR}" PARENT_SCOPE)
    set(CUPUACU_LAME_SOURCE_DIR "${lame_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
