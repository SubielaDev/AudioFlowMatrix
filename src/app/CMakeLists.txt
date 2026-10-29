add_executable(AudioFlowMatrix WIN32
    main.cpp
    App.cpp
    Gui.cpp
    Brand.cpp
    CrashReport.cpp
    app.rc)

target_link_libraries(AudioFlowMatrix PRIVATE
    afm::asio_host
    afm::wasapi
    imgui
    shell32
    dbghelp)
afm_configure_target(AudioFlowMatrix)
string(TIMESTAMP AFM_BUILD_TIME "%Y-%m-%d %H:%M")
target_compile_definitions(AudioFlowMatrix PRIVATE AFM_VERSION_STRING="${PROJECT_VERSION}, built ${AFM_BUILD_TIME}")

if(MINGW)
    target_link_options(AudioFlowMatrix PRIVATE -municode)  # wWinMain entry point
endif()

# Ship the driver DLL next to the executable.
add_dependencies(AudioFlowMatrix AudioFlowMatrixASIO)
