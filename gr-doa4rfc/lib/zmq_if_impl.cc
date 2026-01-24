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

    using input_type = gr_complex;
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
              gr::io_signature::make(1 /* min inputs */, gr::io_signature::IO_INFINITE /* max inputs */, sizeof(input_type)),
              gr::io_signature::make(0, 0, 0)
            ),
        sender_(endpoint)
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
      // Initialize send-buffer
      size_t num_ports = input_items.size();
      std::vector<std::vector<input_type>> send_buffer(num_ports);

      // Iterate over all connected input-ports
      for (size_t port = 0; port < input_items.size(); port++) {
          auto in = static_cast<const input_type*>(input_items[port]);
          send_buffer[port].reserve(noutput_items);
          
          // Copy all available samples to local send-buffer 
          for (int i = 0; i < noutput_items; i++) {
              send_buffer[port].push_back(in[i]);
          }
      }

      // Send received samples from all ports to TCP endpoint 
      sender_.send(send_buffer);

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }

  } /* namespace doa4rfc */
} /* namespace gr */
