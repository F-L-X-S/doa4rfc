/* -*- c++ -*- */
/*
 * Copyright 2026 Felix Schuelke.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_GR_ZMQ_IF_ZMQ_TX_H
#define INCLUDED_GR_ZMQ_IF_ZMQ_TX_H

#include <gnuradio/gr_zmq_if/api.h>
#include <gnuradio/block.h>

namespace gr {
  namespace gr_zmq_if {

    /*!
     * \brief <+description of block+>
     * \ingroup gr_zmq_if
     *
     */
    class GR_ZMQ_IF_API zmq_tx : virtual public gr::block
    {
     public:
      typedef std::shared_ptr<zmq_tx> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of gr_zmq_if::zmq_tx.
       *
       * To avoid accidental use of raw pointers, gr_zmq_if::zmq_tx's
       * constructor is in a private implementation
       * class. gr_zmq_if::zmq_tx::make is the public interface for
       * creating new instances.
       */
      static sptr make(endpoint);
    };

  } // namespace gr_zmq_if
} // namespace gr

#endif /* INCLUDED_GR_ZMQ_IF_ZMQ_TX_H */
