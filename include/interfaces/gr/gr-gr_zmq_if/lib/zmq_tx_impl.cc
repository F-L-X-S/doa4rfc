/* -*- c++ -*- */
/*
 * Copyright 2026 Felix Schuelke.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include "zmq_tx_impl.h"

namespace gr {
  namespace gr_zmq_if {

    #pragma message("set the following appropriately and remove this warning")
    using input_type = float;
    #pragma message("set the following appropriately and remove this warning")
    using output_type = float;
    zmq_tx::sptr
    zmq_tx::make(const std::string& endpoint)
    {
      return gnuradio::make_block_sptr<zmq_tx_impl>(
        endpoint);
    }


    /*
     * The private constructor
     */
    zmq_tx_impl::zmq_tx_impl(const std::string& endpoint)
      : gr::block("zmq_tx",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, sizeof(input_type)),
              gr::io_signature::make(1 /* min outputs */, 1 /*max outputs */, sizeof(output_type))),
        sender_(endpoint)
    {}

    /*
     * Our virtual destructor.
     */
    zmq_tx_impl::~zmq_tx_impl()
    {
    }

    void
    zmq_tx_impl::forecast (int noutput_items, gr_vector_int &ninput_items_required)
    {
    #pragma message("implement a forecast that fills in how many items on each input you need to produce noutput_items and remove this warning")
      /* <+forecast+> e.g. ninput_items_required[0] = noutput_items */
    }

    int
    zmq_tx_impl::general_work (int noutput_items,
                       gr_vector_int &ninput_items,
                       gr_vector_const_void_star &input_items,
                       gr_vector_void_star &output_items)
    {
      auto in = static_cast<const input_type*>(input_items[0]);
      auto out = static_cast<output_type*>(output_items[0]);

      #pragma message("Implement the signal processing in your block and remove this warning")
      // Do <+signal processing+>
      // Tell runtime system how many input items we consumed on
      // each input stream.
      consume_each (noutput_items);

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }


    void zmq_tx_impl::handle_msg(pmt::pmt_t msg)
    {
        using namespace zmq_socket_types;

        if (!pmt::is_u8vector(msg)) {
            throw std::runtime_error("zmq_tx: expected u8vector");
        }

        size_t len;
        const uint8_t* data = pmt::u8vector_elements(msg, len);

        if (len % (2 * sizeof(float)) != 0) {
            throw std::runtime_error("zmq_tx: message length not multiple of complex<float> size");
        }

        size_t num_samples = len / (2 * sizeof(float));
        SampleBatch_t batch;
        batch.reserve(num_samples);

        for (size_t i = 0; i < num_samples; ++i) {
            float real_part;
            float imag_part;

            // Copy Bytes in Floats
            std::memcpy(&real_part, data + i * 2 * sizeof(float), sizeof(float));
            std::memcpy(&imag_part, data + i * 2 * sizeof(float) + sizeof(float), sizeof(float));

            batch.emplace_back(real_part, imag_part);
        }

        sender_.send(batch); 
    }

  } /* namespace gr_zmq_if */
} /* namespace gr */
