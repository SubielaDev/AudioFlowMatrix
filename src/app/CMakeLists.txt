add_executable(AudioFlowMatrix WIN32
    main.cpp
    App.cpp
    Gui.cpp
    Brand.cpp
    app.rc)

target_link_libraries(AudioFlowMatrix PRIVATE
    afm::asio_host
    afm::wasapi
    imgui
    shell32)
afm_configure_target(AudioFlowMatrix)

if(MINGW)
    target_link_options(AudioFlowMatrix PRIVATE -municode)  # wWinMain entry point
endif()

# Ship the driver DLL next to the executable.
add_dependencies(AudioFlowMatrix AudioFlowMatrixASIO)
