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

 using namespace sync_worker_types;

class ZmqSender {
public:
    ZmqSender(const std::string& endpoint);
    void send(const Samples_1dim_t& data);
    void send(const Samples_2dim_t& data);
    void send(const Samples_3dim_t& data);

private:
    zmq::context_t context_;
    zmq::socket_t socket_;
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
 * @brief ZmqRxWorker class receives samples from a ZMQ socket and pushes them into channel-specific rx-sample queues.
 * 
 * @tparam num_channels Number of channels to receive samples for
 */
template <std::size_t num_channels>
class ZmqRxWorker: public MultithreadWorker{
    public:
        /**
         * @brief Construct a new Zmq Rx Worker object for a single channel
         * 
         * @param endpoint 
         * @param rx_queue 
         */
        ZmqRxWorker(        const std::string& endpoint, 
                            std::array<SampleBlockQueue_t, num_channels>& rx_queues, 
                            std::atomic<bool>&          stop_signal_ref):
            MultithreadWorker(stop_signal_ref),
            receiver_(endpoint),
            rx_queues_(rx_queues) {
                for (unsigned int i = 0; i < num_channels; ++i){
                    AddWorkerQueue<SampleBlockQueue_t>(&rx_queues_[i]);
                };  
        };

        /**
         * @brief Destroy the Zmq Rx Worker object
         * 
         */
        ~ZmqRxWorker(){};
    
    private:
        /**
         * @brief The Execute function continuously receives samples from the ZMQ socket 
         * and pushes them into the corresponding channel-specific rx-sample queue for further processing.
         * 
         */
        void Execute() override final {
            uint16_t timestamp;  

            while (!stop_signal_called->load()) {
                // Receive samples from ZMQ socket
                Samples_3dim_t received = receiver_.receive();
                if (received.empty()) continue;

                // Placeholder timestamp
                uint16_t timestamp = static_cast<uint16_t>(std::chrono::steady_clock::now().time_since_epoch().count() % 65536);

                // Push each measurement's per-channel samples into the corresponding rx queues
                for (const auto& measurement : received) {
                    for (unsigned int i = 0; i < num_channels; ++i) {
                        SampleBlock_t sample_block;
                        sample_block.samples = measurement[i];
                        sample_block.timestamp = timestamp;
                        PushItemToQueue<SampleBlock_t>(rx_queues_.at(i), std::move(sample_block));
                    }
                }
            }
        };

        /**
         * @brief Internal ZmqReceiver instance
         * 
         */
        ZmqReceiver receiver_;  
        
        /**
         * @brief Received samples queues for each channel
         * 
         */
        std::array<SampleBlockQueue_t, num_channels>& rx_queues_;
};

/**
 * @brief ZmqTxWorker class sends samples tx-sample queue via a ZMQ socket.
 * 
 */
template <typename tx_item_t>
class ZmqTxWorker: public MultithreadWorker{
    public:
        /**
         * @brief Construct a new Zmq Txx Worker object for a single channel
         * 
         * @param endpoint 
         * @param tx_queue 
         */
        ZmqTxWorker(        const std::string&      endpoint, 
                            ThreadSafeQueue<tx_item_t>&              tx_queue, 
                            std::atomic<bool>&      stop_signal_ref):
            MultithreadWorker(stop_signal_ref),
            sender_(endpoint),
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
                num_popped = PopBatchFromQueue<tx_item_t>(tx_queue_, buffer);
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


#endif // ZMQ_IF