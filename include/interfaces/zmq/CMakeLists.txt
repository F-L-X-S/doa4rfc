# ---------------- External Dependencies ----------------
include(FetchContent)
find_package(PkgConfig REQUIRED)


# ZeroMQ
pkg_check_modules(PC_ZeroMQ QUIET zmq)

## use the hint from above to find where 'zmq.hpp' is located
find_path(ZeroMQ_INCLUDE_DIR
        NAMES zmq.hpp
        PATHS ${PC_ZeroMQ_INCLUDE_DIRS}
        )

## use the hint from above to find the location of libzmq
find_library(ZeroMQ_LIBRARY
        NAMES zmq
        PATHS ${PC_ZeroMQ_LIBRARY_DIRS}
        )

# Boost
find_package(Boost REQUIRED COMPONENTS program_options thread)
include_directories(${Boost_INCLUDE_DIRS})

# ---------------- Create Library ----------------
add_library(zmq_if STATIC
    src/zmq_if.cc
)

# Set include-dirs
target_include_directories(zmq_if 
        PUBLIC 
                include
                ${PROJECT_INCLUDE_DIR} 
                ${ZeroMQ_INCLUDE_DIR}
)

# Set lib-dependencies
target_link_libraries(zmq_if
    PUBLIC
        ${ZeroMQ_LIBRARY}
        multithread_worker
        sync_worker
)