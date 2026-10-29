add_executable(AudioFlowMatrix WIN32
    main.cpp
    App.cpp
    Gui.cpp
    Brand.cpp
    CrashReport.cpp
    Log.cpp
    Ptt.cpp
    Driver.cpp
    app.rc
    ${CMAKE_CURRENT_BINARY_DIR}/driver.rc)

target_link_libraries(AudioFlowMatrix PRIVATE
    afm::asio_host
    afm::wasapi
    afm::midi
    imgui
    shell32
    dbghelp)
afm_configure_target(AudioFlowMatrix)
string(TIMESTAMP AFM_BUILD_TIME "%Y-%m-%d %H:%M")
target_compile_definitions(AudioFlowMatrix PRIVATE AFM_VERSION_STRING="${PROJECT_VERSION}, built ${AFM_BUILD_TIME}")

if(MINGW)
    target_link_options(AudioFlowMatrix PRIVATE -municode)  # wWinMain entry point
    # GUI subsystem, so Windows opens no console window behind the app. CMake's -mwindows
    # for WIN32 executables is silently ignored by zig cc, so say it to the linker directly.
    target_link_options(AudioFlowMatrix PRIVATE "-Wl,--subsystem,windows")
endif()

# The virtual ASIO driver DLL ships next to the executable (installer, zip) and is also
# embedded in it as resource 101 (IDR_ASIO_DLL), so the .exe alone works as a portable app.
add_dependencies(AudioFlowMatrix AudioFlowMatrixASIO)
# The DLL's path varies per generator/configuration, so it is copied to a fixed one that the
# resource script can name and depend on.
set(_afm_embedded_dll ${CMAKE_CURRENT_BINARY_DIR}/embedded/AudioFlowMatrixASIO.dll)
add_custom_command(OUTPUT ${_afm_embedded_dll}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:AudioFlowMatrixASIO> ${_afm_embedded_dll}
    DEPENDS AudioFlowMatrixASIO
    COMMENT "Embedding AudioFlowMatrixASIO.dll")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/driver.rc "101 RCDATA \"${_afm_embedded_dll}\"\n")
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/driver.rc PROPERTIES OBJECT_DEPENDS ${_afm_embedded_dll})
