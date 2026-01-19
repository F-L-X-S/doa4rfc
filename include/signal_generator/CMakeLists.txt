# ---------------- External Dependencies ----------------

# ---------------- Create Library ----------------
add_library(signal_generator STATIC
    src/signal_generator.cc
)

# Set include-dirs
target_include_directories(signal_generator PUBLIC include)
