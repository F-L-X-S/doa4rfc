/* -*- c++ -*- */
/*
 * Copyright 2026 Felix Schuelke.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include "zmq_if_sink_impl.h"

namespace gr {
  namespace doa4rfc {

    // #pragma message("set the following appropriately and remove this warning")
    using input_type = gr_complex;
    zmq_if_sink::sptr
    zmq_if_sink::make(std::string endpoint)
    {
      return gnuradio::make_block_sptr<zmq_if_sink_impl>(
        endpoint);
    }


    /*
     * The private constructor
     */
    zmq_if_sink_impl::zmq_if_sink_impl(std::string endpoint)
      : gr::sync_block("zmq_if_sink",
              gr::io_signature::make(1 /* min inputs */, 1 /* max inputs */, sizeof(input_type)),
              gr::io_signature::make(0, 0, 0)),
              stop_signal(false),
              sender_(endpoint, tx_queue, stop_signal)
    {}

    /*
     * Our virtual destructor.
     */
    zmq_if_sink_impl::~zmq_if_sink_impl()
    {
    }

    /*
     * Flowgraph start 
     */
    bool zmq_if_sink_impl::start() {
      // Start ZMQ transmission worker thread
      stop_signal.store(false);
      sender_.RunWorker();
      return true; // initialization is successful
    }

    /*
     * Flowgraph stop 
     */
    bool zmq_if_sink_impl::stop() {
      sender_.StopWorker();
      return true; // de-initialization is successful
    }

    int
    zmq_if_sink_impl::work(int noutput_items,
        gr_vector_const_void_star &input_items,
        gr_vector_void_star &output_items)
    {
        // Initialize send-buffer
        size_t num_ports = input_items.size();
        std::vector<std::vector<input_type>> send_buffer(num_ports);

        // Iterate over all connected input-ports
        for (size_t port = 0; port < input_items.size(); port++) {
            auto in = static_cast<const input_type*>(input_items[port]);
            send_buffer[port].resize(noutput_items);
            std::memcpy(send_buffer[port].data(), in,
                        noutput_items * sizeof(input_type));
        }

        // Push the send-buffer to the transmission queue for the zmq-tx-worker
        if (!stop_signal.load()){
          std::lock_guard<std::mutex> lock(tx_queue.mtx);
          tx_queue.queue.push(std::forward<std::vector<std::vector<input_type>>>(send_buffer));                            
          tx_queue.cv.notify_one();
        }

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }

  } /* namespace doa4rfc */
} /* namespace gr */
