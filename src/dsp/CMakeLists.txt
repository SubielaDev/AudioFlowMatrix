add_library(afm_dsp STATIC
    MatrixMixer.cpp
    DriftResampler.cpp
    VoiceProcessor.cpp)
add_library(afm::dsp ALIAS afm_dsp)

target_include_directories(afm_dsp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(afm_dsp PUBLIC afm::core PRIVATE afm::rnnoise)
afm_configure_target(afm_dsp)
