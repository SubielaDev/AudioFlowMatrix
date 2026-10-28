# ASIO ABI, sample conversion and the shared-memory bridge protocol.
add_library(afm_asio_common INTERFACE)
add_library(afm::asio_common ALIAS afm_asio_common)
target_include_directories(afm_asio_common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(afm_asio_common INTERFACE afm::core afm::platform)

# Host for the physical master driver + server side of the virtual driver bridge.
add_library(afm_asio_host STATIC
    host/AsioHost.cpp
    host/VirtualAsioServer.cpp)
add_library(afm::asio_host ALIAS afm_asio_host)
target_include_directories(afm_asio_host PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/host>)
target_link_libraries(afm_asio_host PUBLIC afm::asio_common afm::engine advapi32)
afm_configure_target(afm_asio_host)

# "AudioFlow Matrix ASIO": COM in-process driver loaded by Traktor / DAWs.
# Register with: regsvr32 AudioFlowMatrixASIO.dll (administrator).
add_library(AudioFlowMatrixASIO SHARED
    virtual/VirtualAsioDriver.cpp
    virtual/DllMain.cpp
    virtual/AudioFlowMatrixASIO.def)
set_target_properties(AudioFlowMatrixASIO PROPERTIES PREFIX "" OUTPUT_NAME "AudioFlowMatrixASIO")
target_link_libraries(AudioFlowMatrixASIO PRIVATE afm::asio_common advapi32 oleaut32)
afm_configure_target(AudioFlowMatrixASIO)
