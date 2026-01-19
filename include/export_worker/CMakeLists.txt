# ---------------- External Dependencies ----------------
# UHD
find_package(UHD REQUIRED)
include_directories(${UHD_INCLUDE_DIRS})

# Boost
find_package(Boost REQUIRED COMPONENTS program_options thread)
include_directories(${Boost_INCLUDE_DIRS})


# MatlabXport
FetchContent_Declare(
    MatlabXport
    GIT_REPOSITORY https://github.com/F-L-X-S/matlabXport
    GIT_TAG main  
)
FetchContent_MakeAvailable(MatlabXport)

# ---------------- Create Library ----------------
add_library(export_worker STATIC
    src/export_worker.cc
)

# Set include-dirs
target_include_directories(export_worker PUBLIC include)

# Set lib-dependencies
target_link_libraries(export_worker PUBLIC ${UHD_LIBRARIES} ${Boost_LIBRARIES} matlabXport_lib sync_worker multisync sync_worker zmq_if)