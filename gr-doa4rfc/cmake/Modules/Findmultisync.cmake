# Findmultisync.cmake - Find multisync library
find_path(multisync_INCLUDE_DIR
    NAMES multisync.h
    PATHS 
        ${CMAKE_SOURCE_DIR}/../include/multisync/include
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(multisync 
    DEFAULT_MSG
    multisync_INCLUDE_DIR
)

if(multisync_FOUND AND NOT TARGET multisync::multisync)
    add_library(multisync::multisync INTERFACE IMPORTED)
    target_include_directories(multisync::multisync INTERFACE 
        ${multisync_INCLUDE_DIR}
    )
    message(STATUS "Found multisync (header-only): ${multisync_INCLUDE_DIR}")
endif()

mark_as_advanced(multisync_INCLUDE_DIR)