find_package(PkgConfig)

PKG_CHECK_MODULES(PC_GR_DOA4RFC gnuradio-doa4rfc)

FIND_PATH(
    GR_DOA4RFC_INCLUDE_DIRS
    NAMES gnuradio/doa4rfc/api.h
    HINTS $ENV{DOA4RFC_DIR}/include
        ${PC_DOA4RFC_INCLUDEDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/include
          /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    GR_DOA4RFC_LIBRARIES
    NAMES gnuradio-doa4rfc
    HINTS $ENV{DOA4RFC_DIR}/lib
        ${PC_DOA4RFC_LIBDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/lib
          ${CMAKE_INSTALL_PREFIX}/lib64
          /usr/local/lib
          /usr/local/lib64
          /usr/lib
          /usr/lib64
          )

include("${CMAKE_CURRENT_LIST_DIR}/gnuradio-doa4rfcTarget.cmake")

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GR_DOA4RFC DEFAULT_MSG GR_DOA4RFC_LIBRARIES GR_DOA4RFC_INCLUDE_DIRS)
MARK_AS_ADVANCED(GR_DOA4RFC_LIBRARIES GR_DOA4RFC_INCLUDE_DIRS)
