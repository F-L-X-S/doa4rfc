# ---------------- External Dependencies ----------------
# Boost
find_package(Boost REQUIRED COMPONENTS program_options thread)
include_directories(${Boost_INCLUDE_DIRS})

# ---------------- Create Library ----------------
add_library(multithread_worker STATIC
    src/multithread_worker.cc
)

# Set include-dirs
target_include_directories(multithread_worker PUBLIC include)
