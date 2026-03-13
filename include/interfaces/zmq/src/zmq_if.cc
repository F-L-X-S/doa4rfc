/**
 * @file zmq_if.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief This file contains the definition of the ZmqSender class, which is used to send vector-formatted data via ZeroMQ sockets.
 * 
 * @version 0.1
 * @date 2025-05-20
 * 
 * 
 */

#include "zmq_if.h"
#include <cstring>

ZmqSender::ZmqSender(const std::string& endpoint)
    : context_(1), socket_(context_, zmq::socket_type::push)
{
    socket_.connect(endpoint);  // e.g. "tcp://*:5555"
}

/**
 * @brief Send single CSI measurement of single channels
 * 
 * @param data 1-D vector [samplesPerChannel]
 */
void ZmqSender::send(const Samples_1dim_t& data)
{
    Samples_3dim_t wrappedData(1); 
    wrappedData[0].resize(1);  
    wrappedData[0][0] = data;  
    send(wrappedData);
}

/**
 * @brief Send single CSI measurement of multiple channels
 * 
 * @param data 2-D vector [numChannels, samplesPerChannel]
 */
void ZmqSender::send(const Samples_2dim_t& data)
{
    Samples_3dim_t wrappedData(1, data);
    send(wrappedData);
}

/**
 * @brief Send multiple CSI measurements of multiple channels
 * 
 * @param data 3-D vector [numMeasurements, numChannels, samplesPerChannel]
 */
void ZmqSender::send(const Samples_3dim_t& data)
{
    const uint32_t numMeasurements = data.size();
    const uint32_t numChannels = data[0].size();
    const uint32_t samplesPerChannel = data[0][0].size();

    // Header: [numMeasurements, numChannels, samplesPerChannel] → 3 × uint32_t
    std::vector<uint8_t> buffer(sizeof(uint32_t) * 3 + numMeasurements * numChannels * samplesPerChannel * sizeof(Sample_t));

    // Pointer-Offset
    uint8_t* ptr = buffer.data();

    // Write Header
    std::memcpy(ptr, &numMeasurements, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    std::memcpy(ptr, &numChannels, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    std::memcpy(ptr, &samplesPerChannel, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    // Write Data
    for (const auto& measurement : data) {
        for (const auto& ch : measurement) {
            std::memcpy(ptr, ch.data(), ch.size() * sizeof(Sample_t));
            ptr += ch.size() * sizeof(Sample_t);
        }
    }

    // Send message
    zmq::message_t message(buffer.size());
    std::memcpy(message.data(), buffer.data(), buffer.size());
    socket_.send(message, zmq::send_flags::none);
}


ZmqReceiver::ZmqReceiver(const std::string& endpoint)
    : context_(1), socket_(context_, zmq::socket_type::pull) 
{
    socket_.bind(endpoint);  // e.g. "tcp://*:5555"
}

/**
 * @brief Receive batches of samples from multiple channels for multiple measurements
 * 
 * @return 3-D vector [numMeasurements ,numChannels, samplesPerChannel]
 */
Samples_3dim_t ZmqReceiver::receive()
{
    // Receive raw message
    zmq::message_t message;
    if (!socket_.recv(message, zmq::recv_flags::dontwait)) {
        return Samples_3dim_t{}; // No message received
    }

    // Parse Header: 3 × uint32_t [numMeasurements, numChannels, samplesPerChannel]
    if (message.size() < sizeof(uint32_t) * 3) {
        throw std::runtime_error("Invalid ZMQ message: too short for header");
    }

    const uint8_t* ptr = static_cast<const uint8_t*>(message.data());
    uint32_t numMeasurements, numChannels, samplesPerChannel;

    std::memcpy(&numMeasurements, ptr, sizeof(uint32_t));   ptr += sizeof(uint32_t);
    std::memcpy(&numChannels, ptr, sizeof(uint32_t));        ptr += sizeof(uint32_t);
    std::memcpy(&samplesPerChannel, ptr, sizeof(uint32_t));  ptr += sizeof(uint32_t);

    // Validate expected size
    const size_t expectedSize = sizeof(uint32_t) * 3 +
                               numMeasurements * numChannels * samplesPerChannel * sizeof(Sample_t);
    if (message.size() != expectedSize) {
        throw std::runtime_error("Invalid ZMQ message: size mismatch");
    }

    // Allocate result [numMeasurements, numChannels, samplesPerChannel]
    Samples_3dim_t result(numMeasurements, Samples_2dim_t(numChannels, Samples_1dim_t(samplesPerChannel)));
    for (auto& measurement : result) {
        for (auto& channel : measurement) {
            std::memcpy(channel.data(), ptr, samplesPerChannel * sizeof(Sample_t));
            ptr += samplesPerChannel * sizeof(Sample_t);
        }
    }

    return result;

}