# Findzmq_if.cmake - Find zmq_if library
find_path(zmq_if_INCLUDE_DIR
    NAMES zmq_if.h
    PATHS 
        ${CMAKE_SOURCE_DIR}/../include/interfaces/zmq/include
    NO_DEFAULT_PATH
)

# find zmq_if library in build directory
find_library(zmq_if_LIBRARY
    NAMES zmq_if 
    PATHS 
        ${CMAKE_SOURCE_DIR}/../build/ClangDebug/include/interfaces/zmq
        ${CMAKE_SOURCE_DIR}/../build/include/interfaces/zmq
    NO_DEFAULT_PATH
)

# find libzmq on local sytsem 
find_library(LIBZMQ_LIBRARY NAMES zmq PATHS /opt/homebrew/lib /usr/local/lib NO_DEFAULT_PATH)
find_path(LIBZMQ_INCLUDE_DIR zmq.h PATHS /opt/homebrew/include /usr/local/include NO_DEFAULT_PATH)


include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(zmq_if 
    DEFAULT_MSG
    zmq_if_LIBRARY zmq_if_INCLUDE_DIR
)

if(zmq_if_FOUND)
    add_library(zmq_if::zmq_if STATIC IMPORTED)
    set_target_properties(zmq_if::zmq_if PROPERTIES
        IMPORTED_LOCATION "${zmq_if_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${zmq_if_INCLUDE_DIR};${LIBZMQ_INCLUDE_DIR}" 
    )
    target_link_libraries(zmq_if::zmq_if INTERFACE "${LIBZMQ_LIBRARY}") 
    message(STATUS "Found zmq_if: ${zmq_if_LIBRARY}")
endif()

mark_as_advanced(zmq_if_INCLUDE_DIR zmq_if_LIBRARY)
