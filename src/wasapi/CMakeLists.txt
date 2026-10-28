add_library(afm_wasapi STATIC
    WasapiEndpoints.cpp)
add_library(afm::wasapi ALIAS afm_wasapi)

target_include_directories(afm_wasapi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(afm_wasapi PUBLIC afm::engine afm::dsp afm::platform ole32)
afm_configure_target(afm_wasapi)
