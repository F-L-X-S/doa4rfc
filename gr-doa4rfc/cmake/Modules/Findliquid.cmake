# Findliquid.cmake - Find liquid library

# Find path of liquid header file 
find_path(liquid_INCLUDE_DIR
    NAMES liquid.h
    PATHS 
        ${CMAKE_SOURCE_DIR}/../external/liquid-dsp/include
    NO_DEFAULT_PATH
)

# Find path of libliquid.dylib (main lib) 
find_library(liquid_LIBRARY
    NAMES liquid 
    PATHS 
        ${CMAKE_SOURCE_DIR}/../build/ClangDebug/external/liquid-dsp
        ${CMAKE_SOURCE_DIR}/../build//external/liquid-dsp
    NO_DEFAULT_PATH
)

# Also gather versioned dylibs (e.g. libliquid.1.7.0.dylib, libliquid.1.dylib)
file(GLOB LIQUID_VERSIONED_LIBS
    "${CMAKE_SOURCE_DIR}/../build/ClangDebug/external/liquid-dsp/libliquid*.dylib"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(liquid 
    DEFAULT_MSG
    liquid_LIBRARY liquid_INCLUDE_DIR
)

if(liquid_FOUND)
    add_library(liquid::liquid SHARED IMPORTED)
    set_target_properties(liquid::liquid PROPERTIES
        IMPORTED_LOCATION "${liquid_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${liquid_INCLUDE_DIR}"
        LIQUID_VERSIONED_LIBS "${LIQUID_VERSIONED_LIBS}"    # Store all versioned dylibs as property
    )
    message(STATUS "Found liquid: ${liquid_LIBRARY}")
endif()

mark_as_advanced(liquid_INCLUDE_DIR liquid_LIBRARY)
