# Create Library
add_library(sync_worker INTERFACE)

# Set include-dirs
target_include_directories(sync_worker INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_INCLUDE_DIR}
)

# Set lib-dependencies
target_link_libraries(sync_worker 
    INTERFACE 
        multisync 
        multithread_worker
        )