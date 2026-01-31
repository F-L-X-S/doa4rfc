/* -*- c++ -*- */
/*
 * Copyright 2026 Felix Schuelke.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_DOA4RFC_ZMQ_IF_SINK_H
#define INCLUDED_DOA4RFC_ZMQ_IF_SINK_H

#include <gnuradio/doa4rfc/api.h>
#include <gnuradio/sync_block.h>

namespace gr {
  namespace doa4rfc {

    /*!
     * \brief <+description of block+>
     * \ingroup doa4rfc
     *
     */
    class DOA4RFC_API zmq_if_sink : virtual public gr::sync_block
    {
     public:
      typedef std::shared_ptr<zmq_if_sink> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of doa4rfc::zmq_if_sink.
       *
       * To avoid accidental use of raw pointers, doa4rfc::zmq_if_sink's
       * constructor is in a private implementation
       * class. doa4rfc::zmq_if_sink::make is the public interface for
       * creating new instances.
       */
      static sptr make(std::string endpoint="tcp://127.0.0.1:5554");
    };

  } // namespace doa4rfc
} // namespace gr

#endif /* INCLUDED_DOA4RFC_ZMQ_IF_SINK_H */
