/* -*- c++ -*- */
/*
 * Copyright 2026 Felix Schuelke.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_GR_ZMQ_IF_ZMQ_TX_IMPL_H
#define INCLUDED_GR_ZMQ_IF_ZMQ_TX_IMPL_H

#include <gnuradio/gr_zmq_if/zmq_tx.h>
#include <zmq_socket/zmq_socket.h>
#include <pmt/pmt.h>

namespace gr {
  namespace gr_zmq_if {

    class zmq_tx_impl : public zmq_tx
    {
     private:
        ZmqSender sender_;

        void handle_msg(pmt::pmt_t msg);

     public:
      zmq_tx_impl(endpoint);
      ~zmq_tx_impl();

      // Where all the action really happens
      void forecast (int noutput_items, gr_vector_int &ninput_items_required);

      int general_work(int noutput_items,
           gr_vector_int &ninput_items,
           gr_vector_const_void_star &input_items,
           gr_vector_void_star &output_items);

    };

  } // namespace gr_zmq_if
} // namespace gr

#endif /* INCLUDED_GR_ZMQ_IF_ZMQ_TX_IMPL_H */
