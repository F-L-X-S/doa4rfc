# Findsync_worker.cmake - Find sync_worker library
find_path(sync_worker_INCLUDE_DIR
    NAMES sync_worker.h
    PATHS 
        ${CMAKE_SOURCE_DIR}/../include/sync_worker/include
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(sync_worker 
    DEFAULT_MSG
    sync_worker_INCLUDE_DIR
)

if(sync_worker_FOUND AND NOT TARGET sync_worker::sync_worker)
    add_library(sync_worker::sync_worker INTERFACE IMPORTED)
    target_include_directories(sync_worker::sync_worker INTERFACE 
        ${sync_worker_INCLUDE_DIR}
    )
    message(STATUS "Found sync_worker (header-only): ${sync_worker_INCLUDE_DIR}")
endif()

mark_as_advanced(sync_worker_INCLUDE_DIR)