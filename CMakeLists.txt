cmake_minimum_required(VERSION 3.14)
project(doa4rfc)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------- external packages ----------------
include_directories(external)
find_package(PkgConfig REQUIRED)
include(FetchContent)

# FFTW3
pkg_check_modules(FFTW REQUIRED fftw3)
include_directories(${FFTW_INCLUDE_DIRS})
link_directories(${FFTW_LIBRARY_DIRS})

# Liquid-DSP
set(BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(BUILD_AUTOTESTS  OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX    OFF CACHE BOOL "" FORCE)
set(BUILD_DOC        OFF CACHE BOOL "" FORCE)

add_subdirectory(external/liquid-dsp)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/liquid-dsp/include)

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

# Find UHD
find_package(UHD REQUIRED)

# Find Boost
find_package(Boost REQUIRED COMPONENTS program_options thread)

# Include-Paths
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${UHD_INCLUDE_DIRS})

# ---------- internal libraries ----------------
include_directories(include)

# sdr_interfaces
add_library(sdr_interface_uhd_lib include/sdr_interfaces/sdr_interface_uhd.cc)
target_link_libraries(sdr_interface_uhd_lib ${UHD_LIBRARIES} ${Boost_LIBRARIES})

# matlab_export
add_library(matlab_export_lib include/matlab_export/matlab_export.cc)

# zmq_socket
add_library(zmq_socket include/zmq_socket/zmq_socket.cc)
target_link_libraries(zmq_socket cppzmq)

# export_worker
add_library(export_worker_lib include/export_worker/export_worker.cc)
target_link_libraries(export_worker_lib ${UHD_LIBRARIES} ${Boost_LIBRARIES} zmq_socket matlab_export_lib) 

# ui_worker
add_library(ui_worker_lib include/ui_worker/ui_worker.cc)
target_link_libraries(ui_worker_lib ${Boost_LIBRARIES}) 

# signal_generator
add_library(signal_generator_lib include/signal_generator/signal_generator.cc)
target_link_libraries(signal_generator_lib ${Boost_LIBRARIES} liquid) 

# ---------- Main Application  ----------------
# source files
set(MAIN_SRC_FILES
  src/main.cc
)

# executables
add_executable(${PROJECT_NAME} ${MAIN_SRC_FILES})  

# link libraries to executable
target_link_libraries(
  ${PROJECT_NAME} 
    sdr_interface_uhd_lib
    export_worker_lib
    ui_worker_lib
    signal_generator_lib
    matlab_export_lib
    liquid
    zmq_socket
    ${UHD_LIBRARIES}
    ${Boost_LIBRARIES}
  )

# ---------- Simulations ----------------
# Define source files
set(SIMULATIONS_SRC_FILES
  simulations/sim_singlechannel/sim_singlechannel.cc
  simulations/sim_multichannel/sim_multichannel.cc
  simulations/sim_music/sim_music.cc
)

# Iterate over each file in SOURCE_FILES
foreach(SOURCE_FILE ${SIMULATIONS_SRC_FILES})
  # Extract the filename without the directory and extension
  get_filename_component(EXECUTABLE_NAME ${SOURCE_FILE} NAME_WE)

  # Create an executable for each source file
  add_executable(${EXECUTABLE_NAME} ${SOURCE_FILE})

  # Link libraries to the executable
  target_link_libraries(${EXECUTABLE_NAME}
    sdr_interface_uhd_lib
    export_worker_lib
    ui_worker_lib
    signal_generator_lib
    matlab_export_lib
    liquid
    zmq_socket
    ${UHD_LIBRARIES}
    ${Boost_LIBRARIES}
  )

endforeach()

# ---------- Testing ----------------
# include googletest-framework
FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
FetchContent_MakeAvailable(googletest)
enable_testing()

# add test-executable power_trigger
add_executable(power_trigger_test tests/power_trigger_test.cc)

# link libraries to executable
target_link_libraries(
  power_trigger_test
  power_trigger_lib
  GTest::gtest_main
)

# discover tests
include(GoogleTest)
gtest_discover_tests(power_trigger_test)

