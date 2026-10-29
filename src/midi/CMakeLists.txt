# Message decoding and bindings: header-only and portable, so the tests use it anywhere.
add_library(afm_midi_core INTERFACE)
add_library(afm::midi_core ALIAS afm_midi_core)
target_include_directories(afm_midi_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

if(WIN32)
    add_library(afm_midi STATIC MidiInput.cpp)
    add_library(afm::midi ALIAS afm_midi)
    target_link_libraries(afm_midi PUBLIC afm::midi_core PRIVATE afm::platform winmm)
    afm_configure_target(afm_midi)
endif()
