find_package(PkgConfig)

PKG_CHECK_MODULES(PC_GR_GR_ZMQ_IF gnuradio-gr_zmq_if)

FIND_PATH(
    GR_GR_ZMQ_IF_INCLUDE_DIRS
    NAMES gnuradio/gr_zmq_if/api.h
    HINTS $ENV{GR_ZMQ_IF_DIR}/include
        ${PC_GR_ZMQ_IF_INCLUDEDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/include
          /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    GR_GR_ZMQ_IF_LIBRARIES
    NAMES gnuradio-gr_zmq_if
    HINTS $ENV{GR_ZMQ_IF_DIR}/lib
        ${PC_GR_ZMQ_IF_LIBDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/lib
          ${CMAKE_INSTALL_PREFIX}/lib64
          /usr/local/lib
          /usr/local/lib64
          /usr/lib
          /usr/lib64
          )

include("${CMAKE_CURRENT_LIST_DIR}/gnuradio-gr_zmq_ifTarget.cmake")

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GR_GR_ZMQ_IF DEFAULT_MSG GR_GR_ZMQ_IF_LIBRARIES GR_GR_ZMQ_IF_INCLUDE_DIRS)
MARK_AS_ADVANCED(GR_GR_ZMQ_IF_LIBRARIES GR_GR_ZMQ_IF_INCLUDE_DIRS)
