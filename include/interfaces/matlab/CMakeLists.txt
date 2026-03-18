# ---------------- External Dependencies ----------------
# MatlabXport
FetchContent_Declare(
    MatlabXport
    GIT_REPOSITORY https://github.com/F-L-X-S/matlabXport
    GIT_TAG main  
)
FetchContent_MakeAvailable(MatlabXport)

# ---------------- Create Library ----------------
add_library(matlab_if STATIC
    src/matlab_if.cc
)

# Set include-dirs
target_include_directories(matlab_if PUBLIC include ${PROJECT_INCLUDE_DIR})

# Set lib-dependencies
target_link_libraries(matlab_if PUBLIC ${UHD_LIBRARIES} ${Boost_LIBRARIES} matlabXport_lib grouping_worker)