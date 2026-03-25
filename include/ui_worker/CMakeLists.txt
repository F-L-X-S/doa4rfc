# ---------------- External Dependencies ----------------
# Boost
find_package(Boost REQUIRED COMPONENTS program_options thread)
include_directories(${Boost_INCLUDE_DIRS})

# ---------------- Create Library ----------------
add_library(ui_worker STATIC
    src/ui_worker.cc
)

# Set include-dirs
target_include_directories(ui_worker PUBLIC include ${PROJECT_INCLUDE_DIR})

# Set lib-dependencies
target_link_libraries(ui_worker PUBLIC ${Boost_LIBRARIES} sync_worker matlab_if)