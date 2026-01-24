# Findliquid.cmake - Find liquid library
find_path(liquid_INCLUDE_DIR
    NAMES liquid.h
    PATHS 
        ${CMAKE_SOURCE_DIR}/../external/liquid-dsp/include
    NO_DEFAULT_PATH
)

find_library(liquid_LIBRARY
    NAMES liquid 
    PATHS 
        ${CMAKE_SOURCE_DIR}/../build/ClangDebug/external/liquid-dsp
        ${CMAKE_SOURCE_DIR}/../build//external/liquid-dsp
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(liquid 
    DEFAULT_MSG
    liquid_LIBRARY liquid_INCLUDE_DIR
)

if(liquid_FOUND)
    add_library(liquid::liquid UNKNOWN IMPORTED)
    set_target_properties(liquid::liquid PROPERTIES
        IMPORTED_LOCATION "${liquid_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${liquid_INCLUDE_DIR}"
    )
    message(STATUS "Found liquid: ${liquid_LIBRARY}")
endif()

mark_as_advanced(liquid_INCLUDE_DIR liquid_LIBRARY)
