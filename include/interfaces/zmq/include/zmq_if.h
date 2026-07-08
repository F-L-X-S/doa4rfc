/**
 * @file zmq_if.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2026-01-15
 * 
 * 
 */

#ifndef ZMQ_IF
#define ZMQ_IF

#include <zmq.hpp>
#include <vector>
#include <complex>

#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <atomic>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <iostream>

#include <sync_worker.h>
#include <multithread_worker.h>

 using namespace sync_worker_queues;

/**
 * @brief Message type tag transmitted as first header word of every ZMQ message
 *
 */
enum class ZmqMsgType : uint32_t {
    Samples = 0,    // time-domain IQ samples
    Symbols = 1     // demodulated constellation symbols
};

class ZmqSender {
public:
    ZmqSender(const std::string& endpoint, ZmqMsgType type = ZmqMsgType::Samples);
    void send(const Samples_1dim_t& data);
    void send(const Samples_2dim_t& data);
    void send(const Samples_3dim_t& data);

private:
    zmq::context_t context_;
    zmq::socket_t socket_;
    ZmqMsgType type_;
};

class ZmqReceiver {
public:
    ZmqReceiver(const std::string& endpoint);
    Samples_3dim_t receive();

private:
    zmq::context_t context_;
    zmq::socket_t socket_;
};

/**
 * @brief ZmqRxWorker class receives Samples_3dim_t from a ZMQ socket and pushes them into rx-queues,
 * decomposing the received data according to rx_item_t: Samples_3dim_t is forwarded directly,
 * Samples_2dim_t splits by measurement, and Samples_1dim_t splits by measurement and channel.
 *
 * @tparam num_queues Number of channels to receive samples for
 * @tparam rx_item_t Item type of the output queues (Samples_1dim_t, Samples_2dim_t, or Samples_3dim_t)
 */
template <std::size_t num_queues, typename rx_item_t>
class ZmqRxWorker: public MultithreadWorker{
    static_assert(std::is_same_v<rx_item_t, Samples_1dim_t> || std::is_same_v<rx_item_t, SampleBlock_t> || num_queues == 1,
        "num_queues > 1 requires rx_item_t to be Samples_1dim_t or SampleBlock_t since Samples_2dim_t and Samples_3dim_t use only one queue");

    public:
        /**
         * @brief Construct a new Zmq Rx Worker object for a single channel
         * 
         * @param endpoint 
         * @param rx_queue 
         */
        ZmqRxWorker(        const std::string& endpoint,
                            std::array<ThreadSafeQueue<rx_item_t>, num_queues>& rx_queues,
                            std::atomic<bool>&          stop_signal_ref):
            MultithreadWorker(stop_signal_ref),
            receiver_(endpoint),
            rx_queues_(rx_queues) {
                for (unsigned int i = 0; i < num_queues; ++i){
                    AddWorkerQueue<ThreadSafeQueue<rx_item_t>>(&rx_queues_[i]);
                };
        };

        /**
         * @brief Destroy the Zmq Rx Worker object
         * 
         */
        ~ZmqRxWorker(){};
    
    private:
        /**
         * @brief Continuously receives Samples_3dim_t from the ZMQ socket and decomposes them into
         * rx-queue items depending on rx_item_t: Samples_3dim_t forwards the block directly,
         * Samples_2dim_t splits by measurement, Samples_1dim_t splits by measurement and channel,
         * and SampleBlock_t splits by measurement and channel with an added nanosecond timestamp.
         */
        void Execute() override final {
            while (!stop_signal_called->load()) {
                // Receive samples from ZMQ socket
                Samples_3dim_t received = receiver_.receive();
                if (received.empty()) continue;

                if constexpr (std::is_same_v<rx_item_t, Samples_3dim_t>) {
                    // Forward entire received block directly (same type)
                    PushItemToQueue(rx_queues_.at(0), std::move(received));

                } else if constexpr (std::is_same_v<rx_item_t, Samples_2dim_t>) {
                    // Each measurement is a Samples_2dim_t {channel, samples}, forward separately
                    for (auto& measurement : received) {
                        PushItemToQueue(rx_queues_.at(0), std::move(measurement));
                    }

                } else if constexpr (std::is_same_v<rx_item_t, Samples_1dim_t>) {
                    // i-th queue transports the samples of the i-th channel
                    for (auto& measurement : received) {
                        for (unsigned int i = 0; i < num_queues; ++i) {
                            PushItemToQueue(rx_queues_.at(i), std::move(measurement[i]));
                        }
                    }

                } else if constexpr (std::is_same_v<rx_item_t, SampleBlock_t>) {
                    // i-th queue transports timestamped sample blocks for the i-th channel
                    uint64_t timestamp = static_cast<uint64_t>(
                        std::chrono::steady_clock::now().time_since_epoch().count());
                    for (auto& measurement : received) {
                        for (unsigned int i = 0; i < num_queues; ++i) {
                            SampleBlock_t block;
                            block.samples = std::move(measurement[i]);
                            block.timestamp = timestamp;
                            PushItemToQueue(rx_queues_.at(i), std::move(block));
                        }
                    }

                } else {
                    static_assert(!sizeof(rx_item_t*), "rx_item_t must be Samples_1dim_t, Samples_2dim_t, Samples_3dim_t, or SampleBlock_t");
                }
            }
        };

        /**
         * @brief Internal ZmqReceiver instance
         * 
         */
        ZmqReceiver receiver_;  
        
        /**
         * @brief Received item queues for each channel
         * 
         */
        std::array<ThreadSafeQueue<rx_item_t>, num_queues>& rx_queues_;
};

// Deduction guide: deduce num_queues and rx_item_t from the rx_queues array
template <std::size_t N, typename T>
ZmqRxWorker(const std::string&, std::array<ThreadSafeQueue<T>, N>&, std::atomic<bool>&) -> ZmqRxWorker<N, T>;

/**
 * @brief ZmqTxWorker class sends multidimensional sample types from a thread-safe queue to a ZMQ socket.
 * 
 * @tparam tx_item_t Type of the items to send, must be either Samples_1dim_t, Samples_2dim_t or Samples_3dim_t
 * 
 */
template <typename tx_item_t>
class ZmqTxWorker: public MultithreadWorker{
    public:
        /**
         * @brief Construct a new Zmq Tx Worker object for a single channel
         *
         * @param endpoint
         * @param tx_queue
         * @param type message type tag written to the header of every sent message
         */
        ZmqTxWorker(        const std::string&              endpoint,
                            ThreadSafeQueue<tx_item_t>&     tx_queue,
                            std::atomic<bool>&              stop_signal_ref,
                            ZmqMsgType                      type = ZmqMsgType::Samples):
            MultithreadWorker(stop_signal_ref),
            sender_(endpoint, type),
            tx_queue_(tx_queue)
            {
                AddWorkerQueue<ThreadSafeQueue<tx_item_t>>(&tx_queue_);
            };

        /**
         * @brief Destroy the Zmq Rx Worker object
         * 
         */
        ~ZmqTxWorker(){};
    
    private:
        /**
         * @brief The Execute function continuously pushes the samples from the tx-sample queue to the ZMQ socket.
         * 
         */
        void Execute() override final {
            while (!stop_signal_called->load()) {
                // Pop items from tx queue
                std::vector<tx_item_t> buffer;
                size_t num_popped = 0;
                num_popped = PopBatchFromQueue(tx_queue_, buffer);
                if (num_popped == 0) continue;

                // Send item via ZMQ
                if constexpr (std::is_same_v<tx_item_t, Samples_1dim_t> || 
                    std::is_same_v<tx_item_t, Samples_2dim_t> ||
                    std::is_same_v<tx_item_t, Samples_3dim_t>) {
                    for (auto& item : buffer) {
                        sender_.send(item);  
                    }
                } else {
                    static_assert(false, "tx_item_t is no valid type for ZmqSender");
                }

            };
        };

        /**
         * @brief Internal ZmqSender instance
         * 
         */
        ZmqSender sender_;  
        
        /**
         * @brief Reference to transmit queue
         * 
         */
        ThreadSafeQueue<tx_item_t>& tx_queue_;
};

// Deduction guide: deduce tx_item_t from tx_queue
template <typename T>
ZmqTxWorker(const std::string&, ThreadSafeQueue<T>&, std::atomic<bool>&) -> ZmqTxWorker<T>;

// Deduction guide: deduce tx_item_t from tx_queue with explicit message type
template <typename T>
ZmqTxWorker(const std::string&, ThreadSafeQueue<T>&, std::atomic<bool>&, ZmqMsgType) -> ZmqTxWorker<T>;



#endif // ZMQ_IF