/**
 * @file sync_worker.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2025-12-27
 * 
 * 
 */

#ifndef SYNCWORKER_H
#define SYNCWORKER_H     

#include <deque>
#include <limits>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <atomic>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <iostream>

#include <doa4rfc.h>
#include <multithread_worker.h>
#include <multisync.h>

namespace sync_worker_queues {

/**
 * @brief Thread-Safe Queue structure for sample blocks
 * 
 */
using SampleBlockQueue_t = ThreadSafeQueue<SampleBlock_t>;

/**
 * @brief Thread-Safe Queue structure for symbol blocks
 * 
 */
using SymbolBlockQueue_t = ThreadSafeQueue<SymbolBlock_t>;

/**
 * @brief Thread-Safe Queue structure for Frame Samples
 * 
 */
using FrameSampsQueue_t = ThreadSafeQueue<FrameSamps_t>;

/**
 * @brief Thread-Safe Queue structure for Frame Symbols
 * 
 */
using FrameSymsQueue_t = ThreadSafeQueue<FrameSyms_t>;

/**
 * @brief Thread-Safe Queue structure for Phase correction values 
 * 
 */
using PhaseQueue_t = ThreadSafeQueue<Phase_t>;

}   // namespace sync_worker_queues

using namespace doa4rfc;
using namespace sync_worker_queues;

/**
 * @brief The SyncWorker function continuously retrieves timestamped sample-blocks from the channel-specific queues and 
 * executes the synchronization algorithm on these samples through the MultiSync instance. 
 * When a frame is detected by a channel’s synchronizer, the resulting sample-block and associated callback data are pushed into 
 * thread-safe queues for use in subsequent processing stages. 
 * The queued data is tagged with the timestamp and the channel number of the synchronized sample-block, in which the frame was detected.
 * 
 * Throughout the synchronization process, phase corrections can be applied to the NCOs of the MultiSync instance to compensate 
 * the phase errors introduced by the hardware instances.
 * 
 * This function is executed within a dedicated thread using MultithreadWorker.
 * 
 * @tparam num_channels Number of Channels to synchronize
 * @tparam synchronizer_iface Type of the synchronizer interface to use (e.g. ofdmframesync_iface)
 * @param stop_signal_ref Stop signal to terminate the thread
 */
template <std::size_t num_channels, typename synchronizer_iface>
class SyncWorker: public MultithreadWorker {
    public:
        using MsCreateParams_t = MultiSync<synchronizer_iface, num_channels>::CreateParams_t;

        SyncWorker(const MsCreateParams_t&   synchronizer_params,
                    std::atomic<bool>&          stop_signal_ref,
                    int                         record_padding = 0):
                        MultithreadWorker(stop_signal_ref),
                        ms_(synchronizer_params, callback, MakeUserdataPtrs(cb_data_)),
                        record_padding_(record_padding) {
                            frame_samps_queue_ = nullptr;
                            frame_syms_queue_ = nullptr;
                            for (unsigned int ch = 0; ch < num_channels; ++ch) {
                                cb_data_[ch].channel = ch;
                                cb_data_[ch].ms_ptr  = &ms_;
                            }
                            AddWorkerQueue<PhaseQueue_t>(&phi_corr_queue_);         // Add Phase-Corr Queue to Worker

                            for (unsigned int i = 0; i < num_channels; ++i){
                                AddWorkerQueue<SampleBlockQueue_t>(&rx_queues_[i]);
                            };
        };

        ~SyncWorker(){};

        /**
         * @brief Add queue to retrieve sample-blocks belonging to detected frames (Sync-Worker output)
         * 
         * @param queue Queue to retrieve sample-blocks belonging to detected frames
         * @return * void 
         */
        void AddFrameSampsQueue(FrameSampsQueue_t& queue) {
            if (!frame_samps_queue_) {
                frame_samps_queue_ = &queue;
                AddWorkerQueue<FrameSampsQueue_t>(frame_samps_queue_);
            } else {
                AddDuplicateQueue(frame_samps_queue_, &queue);
            }
        };

        /**
         * @brief Add queue to retrieve symbol-blocks belonging to detected frames (Sync-Worker output)
         * 
         * @param queue Queue to retrieve symbol-blocks belonging to detected frames
         * @return * void 
         */
        void AddFrameSymsQueue(FrameSymsQueue_t& queue) {
            if (!frame_syms_queue_) {
                frame_syms_queue_ = &queue;
                AddWorkerQueue<FrameSymsQueue_t>(frame_syms_queue_);
            } else {
                AddDuplicateQueue(frame_syms_queue_, &queue);
            }
        };

        /**
         * @brief Get the reference to the internal rx-sample queues (one for each channel)
         * 
         * @return std::array<SampleBlockQueue_t, num_channels>* Reference to an array of rx-sample queues 
         */
        std::array<SampleBlockQueue_t, num_channels>* GetRxQueues() {
            return &rx_queues_;
        };

        /**
         * @brief Get the reference to a internal rx-sample queue for a specific channel
         * 
         * @return SampleBlockQueue_t*  Reference to channel-specific rx-sample queue
         */
        SampleBlockQueue_t* GetRxQueue(int channel) {
            return &rx_queues_[channel];
        };

        /**
         * @brief Get the reference to a internal Phase-Corr Queue object. 
         * Phase-corrections pushed to the queue are applied by the sync-worker to correct channel specific phase-offsets.
         * 
         * @return PhaseQueue_t& reference to internal Phase-Corr Queue 
         */
        PhaseQueue_t* GetPhaseCorrQueue() {
            return &phi_corr_queue_;
        };

        /**
         * @brief Continuously retrieves sample-blocks from the channel-specific rx-queues and
         * executes the synchronization algorithm sample-by-sample across all channels in lockstep
         * (ch 0 sample[s], ch 1 sample[s], … before advancing to sample[s+1]).
         *
         * Recording lifecycle (driven by DetectFrame / MultiSync):
         *   - Normally record_index_ == 0: MultiSync accumulates a rolling history per channel.
         *   - When a frame is detected, record_index_ is set to frame_len, opening a recording
         *     window in MultiSync that trims all channels' histories to the last frame_len samples
         *     and then continues accumulating.
         *   - After record_countdown_ lockstep steps, record_index_ is set back to 0, which causes
         *     MultiSync to close the recording window on the very next Execute() call.
         *   - snapshot_pending_ flags this transition; the complete multi-channel snapshot is read
         *     from MultiSync, pushed to frame_samps_queue_, and the buffer is cleared.
         *
         * This function is executed within a dedicated thread.
         */
        void Execute() override final {
            // Per-channel deques of incoming sample-blocks (O(1) front removal)
            std::array<std::deque<SampleBlock_t>, num_channels> rx_block_buf;
            bool any_data;

            while (!stop_signal_called->load()) {

                // Process Phase Correction to adjust NCO phase for the channel
                Phase_t phi_corr;
                if (PopItemFromQueue(phi_corr_queue_, phi_corr)){
                    if (static_cast<std::size_t>(phi_corr.channel) >= num_channels) {
                        std::cerr << "Error: Channel " << phi_corr.channel
                                  << " does not exist (valid: 0-" << num_channels - 1 << ")" << std::endl;
                    } else {
                        std::cout << (phi_corr.absolute ? "Set" : "Adjusted") << " NCO of CH" << phi_corr.channel
                                  << " from " << ms_.GetNcoPhase(phi_corr.channel) << " rad";
                        if (phi_corr.absolute)
                            ms_.SetNcoPhase(phi_corr.channel, phi_corr.phi);
                        else
                            ms_.AdjustNcoPhase(phi_corr.channel, phi_corr.phi);
                        std::cout << " to " << ms_.GetNcoPhase(phi_corr.channel) << " rad" << std::endl;
                    }
                    // Print current NCO phases for all channels
                    std::cout << "NCO phases:" << std::endl;
                    for (std::size_t ch = 0; ch < num_channels; ++ch) {
                        std::cout << "  CH" << ch << ": " << ms_.GetNcoPhase(ch) << " rad" << std::endl;
                    }
                }

                // Collect newly available blocks for every channel
                any_data = false;
                for (unsigned int ch = 0; ch < num_channels; ++ch) {
                    std::vector<SampleBlock_t> new_blocks;
                    if (PopBatchFromQueue(rx_queues_[ch], new_blocks, 0) > 0) {
                        for (auto& b : new_blocks)
                            rx_block_buf[ch].push_back(std::move(b));
                        any_data = true;
                    }
                }

                // Process in lockstep: ch0_s[i], ch1_s[i], … before advancing to sample i+1.
                // Only steps if every channel has at least one buffered block.
                bool all_have_data = true;
                while (all_have_data) {
                    // Check if every channel has at least one block buffered; if not, wait for more data to arrive
                    for (unsigned int ch = 0; ch < num_channels; ++ch) {
                        if (rx_block_buf[ch].empty()) { all_have_data = false; break; }
                    }
                    if (!all_have_data) break;

                    // Process as many samples as the smallest front-block allows
                    std::size_t min_samps = std::numeric_limits<std::size_t>::max();
                    for (unsigned int ch = 0; ch < num_channels; ++ch)
                        min_samps = std::min(min_samps, rx_block_buf[ch].front().samples.size());

                    for (std::size_t s = 0; s < min_samps; ++s) {
                        std::array<Sample_t, num_channels> channel_samples;
                        for (unsigned int ch = 0; ch < num_channels; ++ch)
                            channel_samples[ch] = rx_block_buf[ch].front().samples[s];
                        uint64_t timestamp = rx_block_buf[0].front().timestamp; // Assuming aligned timestamps across channels
                        DetectFrame(channel_samples, timestamp);
                        
                        // After every complete lockstep step, advance the recording countdown.
                        // Only decrement when actively recording to avoid unsigned underflow.
                        // When it reaches zero, signal MultiSync to close the window on the next call.
                        if (record_countdown_ > 0) {
                            record_countdown_--;
                            if (record_countdown_ == 0) {
                                record_index_     = 0;
                                snapshot_pending_ = true;   // Snapshot completes on next Execute() transition
                            }
                        }
                    }

                    // Consume processed samples; discard exhausted front blocks
                    for (unsigned int ch = 0; ch < num_channels; ++ch) {
                        auto& front = rx_block_buf[ch].front();
                        front.samples.erase(front.samples.begin(),
                                            front.samples.begin() + static_cast<std::ptrdiff_t>(min_samps));
                        if (front.samples.empty())
                            rx_block_buf[ch].pop_front();
                    }
                }

                // Sleep only when truly idle
                if (!any_data) {
                    bool has_pending = false;
                    for (unsigned int ch = 0; ch < num_channels; ++ch)
                        if (!rx_block_buf[ch].empty()) { has_pending = true; break; }
                    if (!has_pending)
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

            } // while
        }; // Execute

    private:

        /**
         * @brief Callback data structure for userdata shared with the synchronizers in Callback-function
         *
         */
        struct CallbackData_t {
            FrameSyms_t frame_syms;                     // Symbols belonging to detected frames
            MultiSync<synchronizer_iface, num_channels>* ms_ptr;      // Pointer to MultiSync instance
            unsigned int frame_len;                     // length of frame in samples 
            unsigned int channel;                       // Channel index
            bool frame_detected;                        // Frame detected indicator
        };

        /**
         * @brief Per-channel callback-data buffers. Each channel synchronizer receives a pointer
         * to its own entry so that concurrent callbacks carry the correct channel index.
         *
         */
        std::array<CallbackData_t, num_channels> cb_data_;

        /**
         * @brief Internal MultiSync instance
         *
         */
        MultiSync<synchronizer_iface, num_channels> ms_;

        /**
         * @brief Number of additional samples to append beyond the frame length when opening
         * or extending the recording window. Set via constructor parameter.
         */
        int record_padding_ = 0;

        /**
         * @brief record_index passed to ms_.Execute() on every call.
         * 0  = searching mode (MultiSync accumulates rolling history).
         * >0 = recording mode; the value is the window size (frame_len) passed on the first
         *      non-zero call to trim all channel histories and open the snapshot window.
         */
        unsigned int record_index_ = 0;

        /**
         * @brief Idle-sample countdown for the current recording window.
         * Decremented once per complete lockstep step in Execute().
         * Reset to frame_len whenever a callback fires during recording (another symbol arrived).
         * When it reaches 0 without a new callback, no more symbols are expected: record_index_
         * is set back to 0 and snapshot_pending_ is raised to close the window.
         */
        unsigned int record_countdown_ = 0;

        /**
         * @brief Number of trailing noise samples to trim from the snapshot.
         * Set to frame_len each time record_countdown_ is (re-)set. When the countdown expires,
         * exactly this many samples have accumulated since the last callback and must be removed.
         */
        unsigned int snapshot_trim_len_ = 0;

        /**
         * @brief Raised when record_countdown_ hits 0; cleared after the snapshot is pushed.
         * On the first ms_.Execute() call with record_index_ == 0 after this flag is raised,
         * MultiSync closes the recording window and frame_buf_ holds the complete snapshot.
         */
        bool snapshot_pending_ = false;

        /**
         * @brief Timestamp of the rx-block in which the most recent frame was detected.
         * Attached to every FrameSamps_t pushed to frame_samps_queue_.
         */
        uint64_t detection_timestamp_ = 0;

        /**
         * @brief Received samples queues for each channel
         *
         */
        std::array<SampleBlockQueue_t, num_channels> rx_queues_;


        /**
         * @brief Phase correction values queue to adjust NCO phases
         *
         */
        PhaseQueue_t phi_corr_queue_;

        /**
         * @brief External queue to push detected frame samples
         *
         */
        FrameSampsQueue_t* frame_samps_queue_;

        /**
         * @brief External queue to push detected frame data-symbols
         *
         */
        FrameSymsQueue_t* frame_syms_queue_;

        /**
         * @brief Process a single sample for the specified channel.
         *
         * Forwards the sample to ms_.Execute() with the current record_index_, then handles the
         * two events that can result from that call:
         *
         *   Any callback (frame detected on any channel):
         *     - record_countdown_ is reset to frame_len, keeping the recording window open.
         *       When no callback fires for a full symbol-length of lockstep steps the window closes.
         *     - The decoded data symbols are pushed to frame_syms_queue_ immediately for every
         *       callback on every channel, collecting symbols across all channels and all symbols.
         *
         *   First callback only (not yet recording):
         *     - record_index_ is set to frame_len, opening a recording window in MultiSync on the
         *       very next call. MultiSync trims all channel histories to the last frame_len samples
         *       (temporal alignment to frame start) and starts appending from there.
         *     - detection_timestamp_ is captured from the triggering rx_block.
         *
         *   Recording window closed (snapshot_pending_ && MultiSync no longer recording):
         *     - The complete time-aligned multi-channel snapshot is read from MultiSync
         *       (ms_.GetMultiChannelFrameSamps()), one FrameSamps_t per channel is pushed to
         *       frame_samps_queue_, and the snapshot buffer is cleared.
         *
         * @param 
         */
        void DetectFrame(std::array<Sample_t, num_channels>& channel_samples, uint64_t timestamp) {

            // Reset per-channel callback data for this invocation
            for (unsigned int ch = 0; ch < num_channels; ++ch) {
                cb_data_[ch].frame_detected = false;
                cb_data_[ch].frame_len = 0;
                cb_data_[ch].frame_syms.symbols.clear();
            }

            // Run the synchronizer; pass the current recording window size
            ms_.Execute(channel_samples, record_index_);

            // --- Event 1: callback on any channel — opens/extends the recording window ---
            for (unsigned int ch = 0; ch < num_channels; ++ch) {
                if (!cb_data_[ch].frame_detected) continue;

                const unsigned int frame_len = cb_data_[ch].frame_len;

                // Trim accum_buf_ back to the frame start and begin recording.
                // record_countdown_ is set to frame_len: recording stays open as long as
                // callbacks keep arriving (each resets the timer). When no callback fires for
                // a full symbol-length of lockstep steps, the window closes automatically.
                record_countdown_   = frame_len+record_padding_;
                snapshot_trim_len_ = frame_len;

                if (!ms_.IsRecording()) {
                    record_index_        = frame_len+record_padding_;
                    detection_timestamp_ = timestamp;
                }

                // Push decoded data symbols immediately
                if (frame_syms_queue_ && !cb_data_[ch].frame_syms.symbols.empty()) {
                    cb_data_[ch].frame_syms.timestamp = timestamp;
                    cb_data_[ch].frame_syms.channel   = ch;
                    PushItemToQueue(*frame_syms_queue_, FrameSyms_t(cb_data_[ch].frame_syms));
                }
            }

            // --- Event 2: recording window just closed (snapshot complete) ---
            // snapshot_pending_ is raised by Execute() when record_countdown_ hits 0.
            // The first ms_.Execute() call with record_index_ == 0 transitions MultiSync out of
            // recording mode; we detect this here via !ms_.IsRecording().
            if (snapshot_pending_ && !ms_.IsRecording()) {
                if (frame_samps_queue_) {
                    const auto& snapshot = ms_.GetMultiChannelFrameSamps();
                    for (unsigned int push_ch = 0; push_ch < num_channels; ++push_ch) {
                        const auto& raw = snapshot[push_ch];
                        std::size_t trim = std::min(static_cast<std::size_t>(snapshot_trim_len_), raw.size());
                        if (trim < raw.size()) {
                            FrameSamps_t samps;
                            samps.samples.assign(raw.begin(), raw.end() - static_cast<std::ptrdiff_t>(trim));
                            samps.channel   = push_ch;
                            samps.timestamp = detection_timestamp_;
                            PushItemToQueue(*frame_samps_queue_, std::move(samps));
                        }
                    }
                }
                ms_.ClearMultiChannelFrameSamps();
                ms_.Reset();
                snapshot_pending_ = false;
            }
        };

        /**
         * @brief Builds a per-channel array of void* pointers into cb_data for MultiSync construction.
         * Called before ms_ is initialized; safe because cb_data_ is declared before ms_.
         */
        static std::array<void*, num_channels> MakeUserdataPtrs(std::array<CallbackData_t, num_channels>& cb_data) {
            std::array<void*, num_channels> ptrs;
            for (std::size_t i = 0; i < num_channels; ++i)
                ptrs[i] = &cb_data[i];
            return ptrs;
        }

        /**
         * @brief Generic synchronizer callback handler.
         * Called by SyncTraits::Callback via CallbackWrapper after a frame is detected.
         * Each channel's synchronizer passes its own cb_data_[ch] entry, so cb.channel
         * is already set to the correct channel index.
         *
         * @param _cb_data pointer to the channel's CallbackData_t entry
         * @return 0 — synchronizer continues after the callback
         */
        static int callback(void* _cb_data){
            CallbackData_t& cb = *static_cast<CallbackData_t*>(_cb_data);
            cb.frame_detected = true;

            // Add samples to callback-data buffer
            cb.frame_len = cb.ms_ptr->GetFrameLen(cb.channel);   

            // Add data symbols to callback-data buffer 
            cb.ms_ptr->GetFrameSyms(cb.channel, &cb.frame_syms.symbols);

            // Reset after callback (return 1) / don't reset after callback (return 0):
            return 1;
        };
        
};

#endif // SYNCWORKER_H
