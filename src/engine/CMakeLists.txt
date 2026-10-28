add_library(afm_engine STATIC
    AudioEngine.cpp
    RoutingModel.cpp)
add_library(afm::engine ALIAS afm_engine)

target_include_directories(afm_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(afm_engine PUBLIC afm::core afm::dsp)
afm_configure_target(afm_engine)
