add_library(multisync INTERFACE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

target_include_directories(multisync INTERFACE
    ${PROJECT_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Custom IEEE802.11n frame synchronizer (C, uses liquid-dsp public API)
add_library(wlanframesync STATIC src/wlanframesync.c)
target_include_directories(wlanframesync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(wlanframesync PUBLIC liquid)

target_link_libraries(multisync INTERFACE wlanframesync)
