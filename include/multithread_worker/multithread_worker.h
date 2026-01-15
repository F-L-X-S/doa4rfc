/**
 * @file multithread_worker.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief Generic Class to allow multithreaded worker implementations with a fixed number of connected thread-safe queues.
 * @version 0.1
 * @date 2025-12-27
 * 
 * 
 */

#ifndef MULTITHREAD_WORKER_H
#define MULTITHREAD_WORKER_H


#include <queue>       
#include <mutex>                    
#include <condition_variable>         
#include <string>              
#include <atomic>    
#include <vector>
#include <thread>
#include <chrono>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <iostream>

namespace detail {
struct ThreadSafeQueueBase {
    std::mutex mtx;
    std::condition_variable cv;
};
} // namespace detail

/**
 * @brief Thread Safe Queue structure for generic queue item type
 * 
 * @tparam queue_item_t queue item type
 */
template <typename queue_item_t>
struct ThreadSafeQueue: public detail::ThreadSafeQueueBase {
    std::queue<queue_item_t> queue;
};

/**
 * @brief Generic Class to allow multithreaded worker implementations with a fixed number of connected thread-safe queues.
 * 
 * @tparam queus_num Number of connected Queues
 */
class MultithreadWorker {
    public:
        /** 
         * @brief Construct a new Multithread Worker object
         */
        MultithreadWorker(std::atomic<bool>& stop_signal_ref);

        /**
         * @brief Destroy the Multithread Worker object
         * 
         */
        ~MultithreadWorker();

        /**
         * @brief Run Execute function within a separated thread
         * 
         */
        void RunWorker();

        /**
         * @brief Stop thread Execute  function
         * 
         */
        void StopWorker();

    protected:
        /**
         * @brief External stop signal to terminate the threads
         * 
         */
        std::atomic<bool>* stop_signal_called;

        /**
         * @brief Execute function to be implemented by derived worker classes
         * 
         */
        virtual void Execute() = 0;

        /**
         * @brief Add queue to the worker
         * 
         * @param queue Pointer to the thread-safe queue to add
         */
        template <typename T>
        void AddWorkerQueue(detail::ThreadSafeQueueBase* queue) {
            queues_.push_back(static_cast<detail::ThreadSafeQueueBase*>(queue));
        };

    private:
        /**
         * @brief Reference to sync-worker thread
         * 
         */
        std::thread* thread_;

        /**
         * @brief References to connected queues used by the worker
         * 
         */
        std::vector<detail::ThreadSafeQueueBase*> queues_;

};

#endif // MULTITHREAD_WORKER_H