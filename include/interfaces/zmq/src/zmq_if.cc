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

using namespace zmq_socket_types;

ZmqSender::ZmqSender(const std::string& endpoint)
    : context_(1), socket_(context_, zmq::socket_type::push)
{
    socket_.bind(endpoint);  // e.g. "tcp://*:5555"
}

/**
 * @brief Send single CSI measurement of single channels
 * 
 * @param data 1-D vector [samplesPerChannel]
 */
void ZmqSender::send(const SampleBatch_t& data)
{
    MultiMeasurementSampleBatch_t wrappedData(1); 
    wrappedData[0].resize(1);  
    wrappedData[0][0] = data;  
    send(wrappedData);
}

/**
 * @brief Send single CSI measurement of multiple channels
 * 
 * @param data 2-D vector [numChannels, samplesPerChannel]
 */
void ZmqSender::send(const MultiChannelSampleBatch_t& data)
{
    MultiMeasurementSampleBatch_t wrappedData(1, data);
    send(wrappedData);
}

/**
 * @brief Send multiple CSI measurements of multiple channels
 * 
 * @param data 3-D vector [numMeasurements, numChannels, samplesPerChannel]
 */
void ZmqSender::send(const MultiMeasurementSampleBatch_t& data)
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
    : context_(1), socket_(context_, zmq::socket_type::pull)  // ← PULL für Receiver!
{
    socket_.bind(endpoint);  // e.g. "tcp://*:5555"
}

/**
 * @brief Receive batch of samples from a single channel
 * 
 * @return 1-D vector [samplesPerChannel]
 */
SampleBatch_t ZmqReceiver::receive()
{
    auto data = receiveMultiChannel();  // [1, samplesPerChannel]
    return data[0];                  // Unwrap
}

/**
 * @brief Receive batches of samples from multiple channels
 * 
 * @return 2-D vector [numChannels, samplesPerChannel]
 */
MultiChannelSampleBatch_t ZmqReceiver::receiveMultiChannel()
{
    // Receive raw message
    zmq::message_t message;
    if (!socket_.recv(message, zmq::recv_flags::dontwait)) {
        return MultiChannelSampleBatch_t{}; // No message received
    }

    // Parse Header:  × uint32_t
    if (message.size() < sizeof(uint32_t) * 2) {
        throw std::runtime_error("Invalid ZMQ message: too short for header");
    }

    const uint8_t* ptr = static_cast<const uint8_t*>(message.data());
    uint32_t numChannels, samplesPerChannel;

    std::memcpy(&numChannels, ptr, sizeof(uint32_t));     ptr += sizeof(uint32_t);
    std::memcpy(&samplesPerChannel, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);

    // Validate expected size
    const size_t expectedSize = sizeof(uint32_t) * 2 + 
                               numChannels * samplesPerChannel * sizeof(Sample_t);
    if (message.size() != expectedSize) {
        throw std::runtime_error("Invalid ZMQ message: size mismatch");
    }

    // Allocate result
    MultiChannelSampleBatch_t result(numChannels);
    for (auto& channel : result) {
        channel.resize(samplesPerChannel);
        std::memcpy(channel.data(), ptr, samplesPerChannel * sizeof(Sample_t));
        ptr += samplesPerChannel * sizeof(Sample_t);
    }

    return result;
}