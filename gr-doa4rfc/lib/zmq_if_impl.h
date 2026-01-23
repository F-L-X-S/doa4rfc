/* -*- c++ -*- */
/*
 * Copyright 2026 Felix Schuelke.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_DOA4RFC_ZMQ_IF_IMPL_H
#define INCLUDED_DOA4RFC_ZMQ_IF_IMPL_H

#include <gnuradio/doa4rfc/zmq_if.h>

namespace gr {
  namespace doa4rfc {

    class zmq_if_impl : public zmq_if
    {
     private:
      // Nothing to declare in this block.

     public:
      zmq_if_impl(std::string endpoint);
      ~zmq_if_impl();

      // Where all the action really happens
      int work(
              int noutput_items,
              gr_vector_const_void_star &input_items,
              gr_vector_void_star &output_items
      );
    };

  } // namespace doa4rfc
} // namespace gr

#endif /* INCLUDED_DOA4RFC_ZMQ_IF_IMPL_H */
