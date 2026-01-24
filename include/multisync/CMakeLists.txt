add_library(multisync INTERFACE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

target_include_directories(multisync INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
