# ---------------- External Dependencies ----------------
# ZeroMQ
# libzmq (remove for using local zmq installation)
FetchContent_Declare(
  libzmq
  GIT_REPOSITORY https://github.com/zeromq/libzmq.git
  GIT_TAG v4.3.5  
)
FetchContent_MakeAvailable(libzmq)

# ZeroMQ
# cppzmq (C++ Header-Wrapper)
FetchContent_Declare(
  cppzmq
  GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
  GIT_TAG v4.10.0
)
FetchContent_MakeAvailable(cppzmq)
target_include_directories(cppzmq INTERFACE ${cppzmq_SOURCE_DIR})

# Boost
find_package(Boost REQUIRED COMPONENTS program_options thread)
include_directories(${Boost_INCLUDE_DIRS})

# ---------------- Create Library ----------------
add_library(zmq_if STATIC
    src/zmq_if.cc
)

# Set include-dirs
target_include_directories(zmq_if PUBLIC include)

# Set lib-dependencies
target_link_libraries(zmq_if PUBLIC cppzmq)
target_link_libraries(zmq_if PUBLIC multithread_worker_lib)