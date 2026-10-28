add_library(afm_platform INTERFACE)
add_library(afm::platform ALIAS afm_platform)

target_include_directories(afm_platform INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(afm_platform INTERFACE ole32 user32)
afm_configure_target(afm_platform)
