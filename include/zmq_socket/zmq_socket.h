/**
 * @file zmq_socket.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief This file contains the definition of the ZmqSender class, which is used to send vector-formatted data via ZeroMQ sockets.
 * 
 * @version 0.1
 * @date 2025-05-20
 * 
 * 
 */

#ifndef ZMQ_SOCKET_H
#define ZMQ_SOCKET_H

#include <zmq.hpp>
#include <vector>
#include <complex>

namespace zmq_socket_types {
    using Sample_t = std::complex<float>;
    using SampleBatch_t = std::vector<Sample_t>;
    using MultiChannelSampleBatch_t = std::vector<SampleBatch_t>;
    using MultiMeasurementSampleBatch_t = std::vector<MultiChannelSampleBatch_t>;
}

using namespace zmq_socket_types;

class ZmqSender {
public:
    ZmqSender(const std::string& endpoint);
    void send(const SampleBatch_t& data);
    void send(const MultiChannelSampleBatch_t& data);
    void send(const MultiMeasurementSampleBatch_t& data);

private:
    zmq::context_t context_;
    zmq::socket_t socket_;
};

class ZmqReceiver {
public:
    ZmqReceiver(const std::string& endpoint);
    SampleBatch_t receive();
    MultiChannelSampleBatch_t receiveMultiChannel();
    
private:
    zmq::context_t context_;
    zmq::socket_t socket_;
};


#endif // ZMQ_SOCKET_H