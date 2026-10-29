find_package(Threads REQUIRED)

add_executable(afm_tests CoreTests.cpp)
target_link_libraries(afm_tests PRIVATE afm::engine Threads::Threads)
afm_configure_target(afm_tests)

add_test(NAME afm_tests COMMAND afm_tests)

add_executable(afm_voice_tests VoiceTests.cpp)
target_link_libraries(afm_voice_tests PRIVATE afm::dsp)
afm_configure_target(afm_voice_tests)

add_test(NAME afm_voice_tests COMMAND afm_voice_tests)
