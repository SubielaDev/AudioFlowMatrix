# Shared compile settings for every AudioFlow Matrix target.
#
# Usage: afm_configure_target(<target>)
#
# No global -mavx2 / /arch:AVX2: SIMD kernels are compiled per function and selected at
# runtime, so the binaries run on any x64 CPU.

function(afm_configure_target target)
    get_target_property(_type ${target} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY")
        set(_scope INTERFACE)
    else()
        set(_scope PRIVATE)
    endif()

    if(WIN32)
        target_compile_definitions(${target} ${_scope}
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            UNICODE
            _UNICODE)
    endif()

    if(MSVC)
        target_compile_options(${target} ${_scope}
            /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8
            /wd4324                    # "structure was padded due to alignas" - intentional
            $<$<CONFIG:Release,RelWithDebInfo>:/O2 /Ob3 /Oi /GS->)
    else()
        target_compile_options(${target} ${_scope}
            -Wall -Wextra -Wpedantic -Wshadow
            -g  # debug info goes to a separate .pdb, which crash reports use for symbols
            $<$<CONFIG:Release,RelWithDebInfo>:-O3>)
        if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT _type STREQUAL "INTERFACE_LIBRARY")
            target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
        endif()
    endif()
endfunction()
