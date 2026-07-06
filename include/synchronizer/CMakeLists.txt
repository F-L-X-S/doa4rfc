# Custom WLAN frame synchronizer with standard-specific preset initializers
# (C, uses liquid-dsp public API)
add_library(wlanframesync STATIC
    src/wlanframesync.c
    src/wlan_standards.c
)

# Set include-dirs
target_include_directories(wlanframesync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Set lib-dependencies
target_link_libraries(wlanframesync PUBLIC liquid)
