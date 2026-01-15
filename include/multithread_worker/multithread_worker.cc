/**
 * @file multithread_worker.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2026-01-15
 * 
 * 
 */

#include <multithread_worker/multithread_worker.h>

/** 
 * @brief Construct a new Multithread Worker object
 */
MultithreadWorker::MultithreadWorker(std::atomic<bool>& stop_signal_ref):
                stop_signal_called(&stop_signal_ref){
                    thread_ = nullptr;
};

/**
 * @brief Destroy the Multithread Worker object
 * 
 */
MultithreadWorker::~MultithreadWorker(){
    StopWorker();
};

/**
 * @brief Run Execute function within a separated thread
 * 
 */
void MultithreadWorker::RunWorker() {
    if(!thread_) thread_ = new std::thread(&MultithreadWorker::Execute, this);
}

/**
 * @brief Stop thread Execute  function
 * 
 */
void MultithreadWorker::StopWorker() {
    // Set stop signal 
    stop_signal_called->store(true);

    // Wait...
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Notify connected queues
    for (auto& queue : queues_){
        if (queue) queue->cv.notify_all();
    };

    // Kill thread
    if (thread_){
        thread_->join();
        delete thread_;  
    };          
}

