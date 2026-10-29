# RNNoise 0.2 (BSD-3-Clause, see COPYING), xiph/rnnoise release tarball with its built-in model.
# x86 builds compile the SSE4.1 and AVX2 kernels per file and pick one at runtime.
add_library(rnnoise STATIC
    src/denoise.c
    src/rnn.c
    src/pitch.c
    src/kiss_fft.c
    src/celt_lpc.c
    src/nnet.c
    src/nnet_default.c
    src/parse_lpcnet_weights.c
    src/rnnoise_data.c
    src/rnnoise_tables.c)
add_library(afm::rnnoise ALIAS rnnoise)

target_include_directories(rnnoise SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(rnnoise PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(rnnoise PRIVATE RNNOISE_BUILD _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    target_compile_options(rnnoise PRIVATE /W0 $<$<CONFIG:Release,RelWithDebInfo>:/O2>)
else()
    target_compile_options(rnnoise PRIVATE -w -g $<$<CONFIG:Release,RelWithDebInfo>:-O3>)
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|x64")
    target_sources(rnnoise PRIVATE
        src/x86/x86cpu.c
        src/x86/x86_dnn_map.c
        src/x86/nnet_sse4_1.c
        src/x86/nnet_avx2.c)
    target_compile_definitions(rnnoise PRIVATE
        RNN_ENABLE_X86_RTCD
        $<IF:$<C_COMPILER_ID:MSVC>,CPU_INFO_BY_C,CPU_INFO_BY_ASM>)
    if(MSVC)
        set_source_files_properties(src/x86/nnet_avx2.c PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
        # MSVC has no __SSE4_1__ switch; the kernel only needs the intrinsics header.
        set_source_files_properties(src/x86/nnet_sse4_1.c PROPERTIES COMPILE_DEFINITIONS "__SSE4_1__")
    else()
        set_source_files_properties(src/x86/nnet_sse4_1.c PROPERTIES COMPILE_OPTIONS "-msse4.1")
        set_source_files_properties(src/x86/nnet_avx2.c PROPERTIES COMPILE_OPTIONS "-mavx;-mavx2;-mfma")
    endif()
endif()
