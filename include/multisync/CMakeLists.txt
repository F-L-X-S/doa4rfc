add_library(multisync INTERFACE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

target_include_directories(multisync INTERFACE
    ${PROJECT_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Set lib-dependencies (wlanframesync and scframesync targets defined in
# include/synchronizer; synctraits.h includes their headers)
target_link_libraries(multisync INTERFACE wlanframesync scframesync)
