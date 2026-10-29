cmake_minimum_required(VERSION 3.25)

project(AudioFlowMatrix
    VERSION 0.2.0
    DESCRIPTION "Low-latency ASIO/WASAPI routing matrix for Windows"
    LANGUAGES C CXX RC)

if(NOT WIN32)
    message(WARNING "AudioFlow Matrix targets Windows. Only the portable core and tests build elsewhere.")
endif()

option(AFM_BUILD_TESTS "Build unit and stress tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Static C/C++ runtime: the driver DLL is loaded into third-party hosts (Traktor, DAWs)
# and must not depend on a redistributable being installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Executable, driver DLL and tests land next to each other.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Baseline x86-64 for every target, the C/C++ runtime included. zig cc/c++ otherwise
# compiles for the build machine's CPU (-mcpu=native), and the binaries then die with an
# illegal instruction on older CPUs. SIMD kernels opt in per function or per file.
if(CMAKE_CXX_COMPILER MATCHES "zig|zcxx")
    set(_afm_cpu_default "-mcpu=x86_64")
else()
    set(_afm_cpu_default "")
endif()
set(AFM_TARGET_CPU_FLAGS "${_afm_cpu_default}" CACHE STRING "Compile and link flags that pin the target CPU")
if(AFM_TARGET_CPU_FLAGS)
    separate_arguments(_afm_cpu_flags NATIVE_COMMAND "${AFM_TARGET_CPU_FLAGS}")
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:${_afm_cpu_flags}>")
    add_link_options(${_afm_cpu_flags})
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AfmCompilerOptions)

add_subdirectory(src/core)       # afm::core     lock-free rings, triple buffer, aligned memory
add_subdirectory(third_party/rnnoise)  # afm::rnnoise  RNNoise speech denoiser (C)
add_subdirectory(src/dsp)        # afm::dsp      AVX2 matrix mixer, drift resampler, voice processing
add_subdirectory(src/engine)     # afm::engine   AudioEngine, endpoints, routing model
add_subdirectory(src/midi)       # afm::midi     MIDI decoding (portable) + WinMM input

if(WIN32)
    add_subdirectory(src/platform)   # afm::platform  Win32 helpers (COM, MMCSS, UTF-8)
    add_subdirectory(src/asio)       # ASIO host + virtual ASIO driver DLL
    add_subdirectory(src/wasapi)     # WASAPI capture/render endpoints
    add_subdirectory(third_party)    # Dear ImGui
    add_subdirectory(src/app)        # AudioFlowMatrix.exe
endif()

if(AFM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
