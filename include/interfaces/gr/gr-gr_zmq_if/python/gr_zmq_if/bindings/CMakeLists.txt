# Copyright 2020 Free Software Foundation, Inc.
#
# This file is part of GNU Radio
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

########################################################################
# Check if there is C++ code at all
########################################################################
if(NOT gr_zmq_if_sources)
    message(STATUS "No C++ sources... skipping python bindings")
    return()
endif(NOT gr_zmq_if_sources)

########################################################################
# Check for pygccxml
########################################################################
gr_python_check_module_raw("pygccxml" "import pygccxml" PYGCCXML_FOUND)

include(GrPybind)

########################################################################
# Python Bindings
########################################################################

list(APPEND gr_zmq_if_python_files
    zmq_tx_python.cc python_bindings.cc)

gr_pybind_make_oot(gr_zmq_if ../../.. gr::gr_zmq_if "${gr_zmq_if_python_files}")

# copy bindings extension for use in QA test module
add_custom_command(
    TARGET gr_zmq_if_python
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gr_zmq_if_python>
            ${PROJECT_BINARY_DIR}/test_modules/gnuradio/gr_zmq_if/)

install(
    TARGETS gr_zmq_if_python
    DESTINATION ${GR_PYTHON_DIR}/gnuradio/gr_zmq_if
    COMPONENT pythonapi)
