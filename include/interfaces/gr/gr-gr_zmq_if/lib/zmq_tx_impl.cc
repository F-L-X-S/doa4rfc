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
    zmq_tx::make(endpoint)
    {
      return gnuradio::make_block_sptr<zmq_tx_impl>(
        endpoint);
    }


    /*
     * The private constructor
     */
    zmq_tx_impl::zmq_tx_impl(endpoint)
      : gr::block("zmq_tx",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, sizeof(input_type)),
              gr::io_signature::make(1 /* min outputs */, 1 /*max outputs */, sizeof(output_type)),
              sender_(endpoint))
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
        if (!pmt::is_u8vector(msg)) {
            throw std::runtime_error("zmq_tx: expected u8vector");
        }

        size_t len;
        const uint8_t* data =
            pmt::u8vector_elements(msg, len);

        socket_.send(zmq::buffer(data, len), zmq::send_flags::none);
    }

  } /* namespace gr_zmq_if */
} /* namespace gr */
