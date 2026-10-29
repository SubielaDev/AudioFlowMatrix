find_package(Threads REQUIRED)

add_executable(afm_tests CoreTests.cpp)
target_link_libraries(afm_tests PRIVATE afm::engine Threads::Threads)
afm_configure_target(afm_tests)

add_test(NAME afm_tests COMMAND afm_tests)

add_executable(afm_voice_tests VoiceTests.cpp)
target_link_libraries(afm_voice_tests PRIVATE afm::dsp)
afm_configure_target(afm_voice_tests)

add_test(NAME afm_voice_tests COMMAND afm_voice_tests)

add_executable(afm_ptt_midi_tests PttMidiTests.cpp)
target_link_libraries(afm_ptt_midi_tests PRIVATE afm::engine afm::midi_core)
afm_configure_target(afm_ptt_midi_tests)

add_test(NAME afm_ptt_midi_tests COMMAND afm_ptt_midi_tests)
