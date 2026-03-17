# ---------------- External Dependencies ----------------
# Boost
find_package(Boost REQUIRED COMPONENTS program_options thread)
include_directories(${Boost_INCLUDE_DIRS})

# ---------------- Create Library ----------------
add_library(grouping_worker STATIC
    src/grouping_worker.cc
)

# Set include-dirs
target_include_directories(grouping_worker PUBLIC include ${PROJECT_INCLUDE_DIR})

# Set lib-dependencies
target_link_libraries(grouping_worker PUBLIC ${Boost_LIBRARIES} multisync sync_worker zmq_if)