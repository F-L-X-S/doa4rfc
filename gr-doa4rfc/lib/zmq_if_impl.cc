/* -*- c++ -*- */
/*
 * Copyright 2026 Felix Schuelke.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include "zmq_if_impl.h"

namespace gr {
  namespace doa4rfc {

    #pragma message("set the following appropriately and remove this warning")
    using input_type = float;
    zmq_if::sptr
    zmq_if::make(std::string endpoint)
    {
      return gnuradio::make_block_sptr<zmq_if_impl>(
        endpoint);
    }


    /*
     * The private constructor
     */
    zmq_if_impl::zmq_if_impl(std::string endpoint)
      : gr::sync_block("zmq_if",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, sizeof(input_type)),
              gr::io_signature::make(0, 0, 0))
    {}

    /*
     * Our virtual destructor.
     */
    zmq_if_impl::~zmq_if_impl()
    {
    }

    int
    zmq_if_impl::work(int noutput_items,
        gr_vector_const_void_star &input_items,
        gr_vector_void_star &output_items)
    {
      auto in = static_cast<const input_type*>(input_items[0]);

      #pragma message("Implement the signal processing in your block and remove this warning")
      // Do <+signal processing+>

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }

  } /* namespace doa4rfc */
} /* namespace gr */
