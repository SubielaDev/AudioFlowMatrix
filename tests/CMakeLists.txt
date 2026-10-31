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

if(WIN32)
    add_executable(afm_bridge_tests BridgeTests.cpp)
    target_link_libraries(afm_bridge_tests PRIVATE afm::asio_common)
    afm_configure_target(afm_bridge_tests)

    add_test(NAME afm_bridge_tests COMMAND afm_bridge_tests)
endif()

add_executable(afm_update_tests UpdateTests.cpp)
target_include_directories(afm_update_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
afm_configure_target(afm_update_tests)

add_test(NAME afm_update_tests COMMAND afm_update_tests)

add_executable(afm_i18n_tests I18nTests.cpp ${PROJECT_SOURCE_DIR}/src/app/I18n.cpp)
target_include_directories(afm_i18n_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
afm_configure_target(afm_i18n_tests)

add_test(NAME afm_i18n_tests COMMAND afm_i18n_tests)

if(WIN32)
    add_executable(afm_driver_identity_tests DriverIdentityTests.cpp)
    target_include_directories(afm_driver_identity_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
    target_compile_definitions(afm_driver_identity_tests PRIVATE AFM_TEST_DRIVER_DLL="$<TARGET_FILE:AudioFlowMatrixASIO>")
    add_dependencies(afm_driver_identity_tests AudioFlowMatrixASIO)
    afm_configure_target(afm_driver_identity_tests)

    add_test(NAME afm_driver_identity_tests COMMAND afm_driver_identity_tests)
endif()
