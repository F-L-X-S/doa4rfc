# ---------------- External Dependencies ----------------
# UHD
find_package(UHD REQUIRED)
include_directories(${UHD_INCLUDE_DIRS})

# Boost
find_package(Boost REQUIRED COMPONENTS program_options thread)
include_directories(${Boost_INCLUDE_DIRS})

# ---------------- Create Library ----------------
add_library(uhd_if STATIC
    src/uhd_if.cc
)

# Set include-dirs
target_include_directories(uhd_if PUBLIC include ${PROJECT_INCLUDE_DIR})

# Set lib-dependencies
target_link_libraries(uhd_if PUBLIC ${UHD_LIBRARIES} ${Boost_LIBRARIES} sync_worker)