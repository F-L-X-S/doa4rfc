cmake_minimum_required(VERSION 3.14)
project(doa4rfc)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 20)
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


# Find UHD
find_package(UHD REQUIRED)

# Find Boost
find_package(Boost REQUIRED COMPONENTS program_options thread)
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${UHD_INCLUDE_DIRS})

# MatlabXport
FetchContent_Declare(
    matlabXport
    GIT_REPOSITORY https://github.com/F-L-X-S/matlabXport
    GIT_TAG main  
)
FetchContent_MakeAvailable(matlabXport)

# ---------- internal libraries ----------------
add_subdirectory(include/multisync)
add_subdirectory(include/sync_worker)
add_subdirectory(include/multithread_worker)
add_subdirectory(include/interfaces/uhd)
add_subdirectory(include/interfaces/zmq)
add_subdirectory(include/export_worker)
add_subdirectory(include/signal_generator)
add_subdirectory(include/ui_worker)

include_directories(include)

# ---------- Gnu-Radio Blocks  ----------------
find_package(Gnuradio REQUIRED)

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
    multithread_worker
    uhd_if
    export_worker
    ui_worker
    signal_generator
    matlabXport_lib
    liquid
    zmq_if
    ${UHD_LIBRARIES}
    ${Boost_LIBRARIES}
  )

# ---------- Simulations ----------------
# Define source files
set(SIMULATIONS_SRC_FILES
  simulations/sim_singlechannel/sim_singlechannel.cc
  simulations/sim_multichannel/sim_multichannel.cc
  simulations/sim_music/sim_music.cc
  simulations/sim_music_singlecarrier/sim_music_singlecarrier.cc
  simulations/music_gnuradio/music_gnuradio.cc
)

# Iterate over each file in SOURCE_FILES
foreach(SOURCE_FILE ${SIMULATIONS_SRC_FILES})
  # Extract the filename without the directory and extension
  get_filename_component(EXECUTABLE_NAME ${SOURCE_FILE} NAME_WE)

  # Create an executable for each source file
  add_executable(${EXECUTABLE_NAME} ${SOURCE_FILE})

  # Link libraries to the executable
  target_link_libraries(${EXECUTABLE_NAME}
    multithread_worker
    uhd_if
    export_worker
    ui_worker
    signal_generator
    matlabXport_lib
    liquid
    zmq_if
    ${UHD_LIBRARIES}
    ${Boost_LIBRARIES}
  )

endforeach()

# # ---------- Testing ----------------
# # include googletest-framework
# FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/heads/main.zip
# )
# #FetchContent_MakeAvailable(googletest)
# enable_testing()

# # add test-executable power_trigger
# add_executable(power_trigger_test tests/power_trigger_test.cc)

# # link libraries to executable
# target_link_libraries(
#   power_trigger_test
#   power_trigger_lib
#   GTest::gtest_main
# )

# # discover tests
# include(GoogleTest)
# gtest_discover_tests(power_trigger_test)

