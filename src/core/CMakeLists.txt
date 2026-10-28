# Header-only real-time primitives. Nothing in here allocates after construction.
add_library(afm_core INTERFACE)
add_library(afm::core ALIAS afm_core)

target_include_directories(afm_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(afm_core INTERFACE cxx_std_20)
afm_configure_target(afm_core)
