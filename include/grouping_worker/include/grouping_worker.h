/**
 * @file grouping_worker.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2026-03-17
 * 
 * 
 */

#ifndef GROUPING_WORKER_H
#define GROUPING_WORKER_H

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

#include <uhd/usrp/multi_usrp.hpp>   

#include <multithread_worker.h>
#include <sync_worker.h>
#include <zmq_if.h>
#include <matlabXport.hpp>

#include <doa4rfc.h>

namespace grouping_worker_queues {

/**
 * @brief Thread-Safe Queue structure for multi-channel sample blocks belonging to one frame (grouped by timestamp and channel)
 * [channel, sample_index]
 * 
 */
using MultiChSampsQueue_t = ThreadSafeQueue<Samples_2dim_t>;

/**
 * @brief Thread-Safe Queue structure for multi-channel symbol blocks belonging to one frame (grouped by timestamp and channel)
 * [channel, symbol_index]
 * 
 */
using MultiChSymsQueue_t = ThreadSafeQueue<Symbols_2dim_t>;

}   // namespace grouping_worker_queues

using namespace doa4rfc;
using namespace sync_worker_queues;
using namespace grouping_worker_queues;


/**
 * @brief The GroupingWorker identifies groups of per-channel frame samples and symbols whose timestamps
 * all fall within a configurable time range (max_age) and whose channel indices are all unique within
 * that group. For each complete group (all num_channels_ channels represented), the per-channel data is
 * assembled into a multi-channel vector (Samples_2dim_t / Symbols_2dim_t) and pushed to the
 * corresponding output queue.
 *
 * The grouping algorithm operates on timestamp-sorted buffers: for each candidate base entry, it scans
 * forward to collect entries from other channels within the max_age window. If all num_channels_ channels
 * are represented, the group is exported and processed entries are removed from the buffer. Otherwise,
 * old entries are cleared while retaining a minimum number to allow grouping across iterations.
 *
 * This worker is executed within a dedicated thread.
 *
 * @tparam num_channels Number of channels that must be represented in a complete group
 */
class GroupingWorker: public MultithreadWorker {
    public:
        GroupingWorker( std::size_t num_channels,
                        uint64_t                    max_age,
                        std::atomic<bool>&          stop_signal_ref);

        ~GroupingWorker(){};

        /**
         * @brief Get the reference to the internal frame samples queue to push samples to the worker
         * (timestamped sample vector belonging to one frame and channel)
         * 
         * @return FrameSampsQueue_t*  Reference to the internal frame samples queue
         */
        FrameSampsQueue_t* GetFrameSampsQueue();

        /**
         * @brief Get the reference to the internal frame data-symbols queue to push symbols to the worker 
         * (timestamped symbol vector belonging to one frame and channel)
         * 
         * @return FrameSampsQueue_t*  Reference to the internal frame data-symbols queue
         */
        FrameSymsQueue_t* GetFrameSymsQueue();

        /**
         * @brief Add queue to retrieve multi-channel frame samples (timestamped sample vector belonging to one frame and channel)
         * [channel, sample_index]
         * 
         * @param queue Queue to retrieve multi-channel frame samples
         * @return * void 
         */
        void AddMultiChSampsQueue(ThreadSafeQueue<Samples_2dim_t>& queue);

        /**
         * @brief Add queue to retrieve multi-channel frame symbols (timestamped symbol vector belonging to one frame and channel)
         * [channel, symbol_index]
         * 
         * @param queue Queue to retrieve multi-channel frame symbols
         * @return * void 
         */
        void AddMultiChSymsQueue(ThreadSafeQueue<Symbols_2dim_t>& queue);

    protected:

        /**
         * @brief 
         * 
         * This function is executed within a dedicated thread.
         * 
         * @param stop_signal_called Stop signal to terminate the thread
         */
        void Execute() override final;

    private:
        /**
         * @brief Sort a buffer of timestamped items by ascending timestamp
         */
        template <typename T>
        static void SortByTimestamp(std::vector<T>& buffer);

        /**
         * @brief Identifies groups of samples / symbols across channels falling within the max_age time range and pushes them to the multi-channel queues
         */
        template <typename T>
        void ExportGroups(std::vector<T>& buffer);

        /**
         * @brief Removes all entries from the buffer, retaining search_offset entries
         * @param buffer Buffer to clear
         * @param search_offset Number of entries to retain in the buffer
         */
        template <typename T>
        static void ClearBuffer(std::vector<T>& buffer, unsigned int search_offset);

        /**
         * @brief Number of channels that must be represented in a complete group   
         * 
         */
        std::size_t num_channels_;                          

        /**
         * @brief Time-range within which the timestamps of block corresponding to one frame must fall
         *
         */
        uint64_t max_age_;                             

        /**
         * @brief Queue to receive detected frame samples (timestamped sample vector belonging to one frame and channel)
         * 
         */
        FrameSampsQueue_t frame_samps_queue_;

        /**
         * @brief Queue to receive detected frame data-symbols (timestamped symbol vector belonging to one frame and channel)
         * 
         */
        FrameSymsQueue_t frame_syms_queue_;

        /**
         * @brief External queue to push detected multi-channel frame samples (timestamped sample vector belonging to one frame and channel)
         * [channel, sample_index]
         * 
         */
        MultiChSampsQueue_t* multich_samps_queue_;

        /**
         * @brief External queue to push detected multi-channel frame symbols (timestamped symbol vector belonging to one frame and channel)
         * [channel, symbol_index]
         * 
         */
        MultiChSymsQueue_t* multich_syms_queue_;
};


#endif // GROUPING_WORKER_H