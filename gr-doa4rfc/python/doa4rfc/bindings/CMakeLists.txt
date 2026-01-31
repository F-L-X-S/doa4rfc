# Copyright 2020 Free Software Foundation, Inc.
#
# This file is part of GNU Radio
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

########################################################################
# Check if there is C++ code at all
########################################################################
if(NOT doa4rfc_sources)
    message(STATUS "No C++ sources... skipping python bindings")
    return()
endif(NOT doa4rfc_sources)

########################################################################
# Check for pygccxml
########################################################################
gr_python_check_module_raw("pygccxml" "import pygccxml" PYGCCXML_FOUND)

include(GrPybind)

########################################################################
# Python Bindings
########################################################################
list(APPEND doa4rfc_python_files
    zmq_if_sink_python.cc python_bindings.cc)

gr_pybind_make_oot(doa4rfc ../../.. gr::doa4rfc "${doa4rfc_python_files}")

# copy bindings extension for use in QA test module
add_custom_command(
    TARGET doa4rfc_python
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:doa4rfc_python>
            ${PROJECT_BINARY_DIR}/test_modules/gnuradio/doa4rfc/)

install(
    TARGETS doa4rfc_python
    DESTINATION ${GR_PYTHON_DIR}/gnuradio/doa4rfc
    COMPONENT pythonapi)
