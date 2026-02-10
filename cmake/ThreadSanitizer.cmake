function(cupuacu_enable_tsan_for_target target out_enabled_var)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "cupuacu_enable_tsan_for_target: target '${target}' does not exist")
    endif()

    set(_enable_tsan OFF)

    if(APPLE)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(_enable_tsan ON)
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(_enable_tsan ON)
        endif()
    endif()

    if(_enable_tsan)
        target_compile_options("${target}" PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options("${target}" PRIVATE -fsanitize=thread)
        target_compile_options("${target}" PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-O2>)
        target_compile_definitions("${target}" PRIVATE CUPUACU_TSAN_ENABLED=1)
    else()
        target_compile_definitions("${target}" PRIVATE CUPUACU_TSAN_ENABLED=0)
    endif()

    set(${out_enabled_var} ${_enable_tsan} PARENT_SCOPE)
endfunction()
