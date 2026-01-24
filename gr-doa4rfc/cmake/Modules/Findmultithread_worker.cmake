# Findmultithread_worker.cmake - Find multithread_worker library
find_path(multithread_worker_INCLUDE_DIR
    NAMES multithread_worker.h
    PATHS 
        ${CMAKE_SOURCE_DIR}/../include/multithread_worker/include
    NO_DEFAULT_PATH
)

find_library(multithread_worker_LIBRARY
    NAMES multithread_worker 
    PATHS 
        ${CMAKE_SOURCE_DIR}/../build/ClangDebug/include//multithread_worker
        ${CMAKE_SOURCE_DIR}/../build/include/multithread_worker
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(multithread_worker 
    DEFAULT_MSG
    multithread_worker_LIBRARY multithread_worker_INCLUDE_DIR
)

if(multithread_worker_FOUND)
    add_library(multithread_worker::multithread_worker UNKNOWN IMPORTED)
    set_target_properties(multithread_worker::multithread_worker PROPERTIES
        IMPORTED_LOCATION "${multithread_worker_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${multithread_worker_INCLUDE_DIR}"
    )
    message(STATUS "Found multithread_worker: ${multithread_worker_LIBRARY}")
endif()

mark_as_advanced(multithread_worker_INCLUDE_DIR multithread_worker_LIBRARY)
