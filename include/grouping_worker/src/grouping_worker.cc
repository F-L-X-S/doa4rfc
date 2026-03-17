/**
 * @file grouping_worker.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 *
 * @brief
 * @version 0.1
 * @date 2026-03-17
 *
 *
 */

#include <grouping_worker.h>

GroupingWorker::GroupingWorker( std::size_t num_channels,
                                uint64_t max_age,
                                std::atomic<bool>& stop_signal_ref):
                                max_age_(max_age),
                                num_channels_(num_channels),
                                MultithreadWorker(stop_signal_ref)
{
    AddWorkerQueue<FrameSampsQueue_t>(&frame_samps_queue_);
    AddWorkerQueue<FrameSymsQueue_t>(&frame_syms_queue_);
    multich_samps_queue_ = nullptr;
    multich_syms_queue_ = nullptr;
};

GroupingWorker::~GroupingWorker(){};

/**
 * @brief Get the reference to the internal frame samples queue to push samples to the worker
 * (timestamped sample vector belonging to one frame and channel)
 *
 * @return FrameSampsQueue_t*  Reference to the internal frame samples queue
 */
FrameSampsQueue_t* GroupingWorker::GetFrameSampsQueue() {
    return &frame_samps_queue_;
};

/**
 * @brief Get the reference to the internal frame data-symbols queue to push symbols to the worker
 * (timestamped symbol vector belonging to one frame and channel)
 *
 * @return FrameSampsQueue_t*  Reference to the internal frame data-symbols queue
 */
FrameSymsQueue_t* GroupingWorker::GetFrameSymsQueue() {
    return &frame_syms_queue_;
};

/**
 * @brief Add queue to retrieve multi-channel frame samples (timestamped sample vector belonging to one frame and channel)
 * [channel, sample_index]
 *
 * @param queue Queue to retrieve multi-channel frame samples
 */
void GroupingWorker::AddMultiChSampsQueue(ThreadSafeQueue<Samples_2dim_t>& queue) {
    multich_samps_queue_ = &queue;
    AddWorkerQueue<ThreadSafeQueue<Samples_2dim_t>>(multich_samps_queue_);
};

/**
 * @brief Add queue to retrieve multi-channel frame symbols (timestamped symbol vector belonging to one frame and channel)
 * [channel, symbol_index]
 *
 * @param queue Queue to retrieve multi-channel frame symbols
 */
void GroupingWorker::AddMultiChSymsQueue(ThreadSafeQueue<Symbols_2dim_t>& queue) {
    multich_syms_queue_ = &queue;
    AddWorkerQueue<ThreadSafeQueue<Samples_2dim_t>>(multich_syms_queue_);
};

/**
 * @brief This function is executed within a dedicated thread.
 *
 * @param stop_signal_called Stop signal to terminate the thread
 */
void GroupingWorker::Execute() {

    while (!stop_signal_called->load()) {
        // Non-blocking pop from queues to buffers
        std::vector<FrameSamps_t> frame_samps_buffer, frame_syms_buffer;
        if (0 == PopBatchFromQueue<SampleBlock_t>(frame_samps_queue_, frame_samps_buffer, 0) &&
            0 == PopBatchFromQueue<SampleBlock_t>(frame_syms_queue_, frame_syms_buffer, 0))
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Avoid busy-waiting when no channel has data
            continue;   // No samples available

        // Sort buffers by timestamp
        SortByTimestamp(frame_samps_buffer);
        SortByTimestamp(frame_syms_buffer);

        // Export complete groups
        ExportGroups(frame_samps_buffer);
        ExportGroups(frame_syms_buffer);

        // Clear all processed entries from the buffer, keep min. one entry for each channel to find groups across iterations
        ClearBuffer(frame_samps_buffer, num_channels_);
        ClearBuffer(frame_syms_buffer, num_channels_);
    } // while
};

/**
 * @brief Sort a buffer of timestamped items by ascending timestamp
 */
template <typename T>
void GroupingWorker::SortByTimestamp(std::vector<T>& buffer) {
    std::sort(buffer.begin(), buffer.end(),
        [](const T& a, const T& b) { return a.timestamp < b.timestamp; });
};

/**
 * @brief Identifies groups of samples / symbols across channels falling within the max_age time range and pushes them to the multi-channel queues
 */
template <typename T>
void GroupingWorker::ExportGroups(std::vector<T>& buffer) {
    for (unsigned int i = 0; i < buffer.size(); ++i) {
        // Initialize potential group with nullptrs and add initial item at channel index
        std::vector<const T*> group(num_channels_, nullptr); // Potential group of items across channels
        const auto& base = buffer[i];                       // Add initial item to group
        group[base.channel] = &base;

        // Find all items around the base-item within the max_age window
        unsigned int j;
        for (j = i + 1; j < buffer.size(); ++j) {
            if ((buffer[j].timestamp - base.timestamp) > max_age_)
                // timestamp out of range -> following timestamps will also be out of range
                break;

            if (!group[buffer[j].channel])
                // timestamp in range and channel not yet represented in group -> add to group
                group[buffer[j].channel] = &buffer[j];
        }

        // Check if group is complete (all channels represented)
        bool complete = std::all_of(group.begin(), group.end(),
            [](const T* ptr) { return ptr != nullptr; });

        // Export complete group to multi-channel queue
        if (complete) {
            if constexpr (std::is_same_v<T, FrameSamps_t>) {
                Samples_2dim_t multich_data(num_channels_);
                for (const auto& item : group)
                    multich_data[item->channel] = item->samples;
                if (multich_samps_queue_)
                    PushItemToQueue<Samples_2dim_t>(*multich_samps_queue_, std::move(multich_data));

            } else if constexpr (std::is_same_v<T, FrameSyms_t>) {
                Symbols_2dim_t multich_data(num_channels_);
                for (const auto& item : group)
                    multich_data[item->channel] = item->symbols;
                if (multich_syms_queue_)
                    PushItemToQueue<Symbols_2dim_t>(*multich_syms_queue_, std::move(multich_data));
            }

            // Remove processed entries
            buffer.erase(buffer.begin(), buffer.begin() + j);
            break;
        }
    }
};

/**
 * @brief Removes all entries from the buffer, retaining search_offset entries
 * @param buffer Buffer to clear
 * @param search_offset Number of entries to retain in the buffer
 */
template <typename T>
void GroupingWorker::ClearBuffer(std::vector<T>& buffer, unsigned int search_offset) {
    if (buffer.size() > num_channels_) {
        int rest = static_cast<int>(buffer.size()) - static_cast<int>(search_offset) - 1;
        buffer.erase(buffer.begin(), buffer.end() - rest);
    }
};
