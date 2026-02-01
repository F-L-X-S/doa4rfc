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

        /**
         * @brief Push item to the specified thread-safe queue
         * 
         * @tparam queue_item_t Type of the queue item
         * @param q Reference to the thread-safe queue
         * @param item Item to push to the queue
         */
        template <typename queue_item_t>
        void PushItemToQueue(ThreadSafeQueue<queue_item_t>& q ,queue_item_t&& item) {
            std::lock_guard<std::mutex> lock(q.mtx);
            q.queue.push(std::forward<queue_item_t>(item));                            
            q.cv.notify_one();
        };

        /**
         * @brief Push batch of items to the specified thread-safe queue
         * 
         * @tparam queue_item_t Type of the queue item
         * @param q Reference to the thread-safe queue
         * @param buffer Vector containing the items to push to the queue
         */
        template <typename queue_item_t>
        void PushBatchToQueue(ThreadSafeQueue<queue_item_t>& q , std::vector<queue_item_t>& buffer) {
            std::lock_guard<std::mutex> lock(q.mtx);
            while (!buffer.empty()) {
                q.queue.push(std::move(buffer.back()));
                buffer.pop_back(); 
            };                           
            q.cv.notify_one();
        };

        /**
         * @brief Pop single item from the specified thread-safe queue to the provided buffer
         * 
         * @tparam queue_item_t Type of the queue item
         * @param q Reference to the thread-safe queue
         * @param buffer buffer to store the popped item
         * @return bool Indicator, if an Item was popped from the queue
         */
        template <typename queue_item_t>
        bool PopItemFromQueue(ThreadSafeQueue<queue_item_t>& q, queue_item_t& buffer) {
                std::unique_lock<std::mutex> lock(q.mtx);
                q.cv.wait(lock, [&q, this] { 
                    return !q.queue.empty() || stop_signal_called->load(); 
                });

                if (!q.queue.empty()) {
                    buffer = std::move(q.queue.front());
                    q.queue.pop();
                    return true;
                }
        };

        /**
         * @brief Pop batch of items from the specified thread-safe queue to the provided buffer
         * 
         * @tparam queue_item_t Type of the queue item
         * @param q Reference to the thread-safe queue
         * @param buffer Vector to store the popped items
         * @param max_items Max. number of items to pop from the queue
         * @return size_t Number of items actually popped from the queue
         */
        template <typename queue_item_t>
        size_t PopItemFromQueue(ThreadSafeQueue<queue_item_t>& q, std::vector<queue_item_t>& buffer, size_t max_items) {
                std::unique_lock<std::mutex> lock(q.mtx);
                q.cv.wait(lock, [&q, this] { 
                    return !q.queue.empty() || stop_signal_called->load(); 
                });

                size_t popped = 0;
                while (popped < max_items && !q.queue.empty()) {
                    buffer.emplace_back(std::move(q.queue.front()));
                    q.queue.pop();
                    ++popped;
                }
                return popped;
        };

        /**
         * @brief Pop all items from the specified thread-safe queue to the provided buffer
         * 
         * @tparam queue_item_t Type of the queue item
         * @param q Reference to the thread-safe queue
         * @param buffer Vector to store the popped items
         * @return size_t Number of items actually popped from the queue
         */
        template <typename queue_item_t>
        size_t PopBatchFromQueue(ThreadSafeQueue<queue_item_t>& q, std::vector<queue_item_t>& buffer) {
                std::unique_lock<std::mutex> lock(q.mtx);
                q.cv.wait(lock, [&q, this] { 
                    return !q.queue.empty() || stop_signal_called->load(); 
                });

                size_t popped = 0;
                while (!q.queue.empty()) {
                    buffer.emplace_back(std::move(q.queue.front()));
                    q.queue.pop();
                    ++popped;
                }
                return popped;
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