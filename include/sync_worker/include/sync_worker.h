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

#include <multithread_worker.h>
#include <multisync.h>

/**
 * @brief Single Sample Type
 * 
 */
using Sample_t = std::complex<float>;                   // Received samples type

/**
 * @brief Detected Symbol type
 * 
 */
using Symbol_t = std::complex<float>;                   // Detected Symbol type

/**
 * @brief Block of samples with timestamp
 * 
 */
struct SampleBlock_t
{
    std::vector<Sample_t> samples;                      // Received samples
    uint64_t timestamp;                                 // Global Nano-Second-Timestamp
};

/**
 * @brief Thread-Safe Queue structure for sample blocks
 * 
 */
using SampleBlockQueue_t = ThreadSafeQueue<SampleBlock_t>;

/**
 * @brief Block of symbols with timestamp
 * 
 */
struct SymbolBlock_t
{
    std::vector<Symbol_t> symbols;                      // Received symbols
    uint64_t timestamp;                                 // Global Nano-Second-Timestamp
};

/**
 * @brief Thread-Safe Queue structure for symbol blocks
 * 
 */
using SymbolBlockQueue_t = ThreadSafeQueue<SymbolBlock_t>;



/**
 * @brief Sample-Block belonging to one frame 
 * 
 */
struct FrameSamps_t: public SampleBlock_t {
    unsigned int channel;                               // Channel index
};

/**
 * @brief Thread-Safe Queue structure for Frame Samples
 * 
 */
using FrameSampsQueue_t = ThreadSafeQueue<FrameSamps_t>;

/**
 * @brief Symbol-Block belonging to one frame 
 * 
 */
struct FrameSyms_t: public SymbolBlock_t {
    unsigned int channel;                               // Channel index
};

/**
 * @brief Thread-Safe Queue structure for Frame Symbols
 * 
 */
using FrameSymsQueue_t = ThreadSafeQueue<FrameSyms_t>;

/**
 * @brief Samples of all channels belonging to one frame 
 * 
 */
using  MultiChFrameSamps_t = std::vector<std::vector<Sample_t>>;

/**
 * @brief Thread-Safe Queue structure for Frame Samples
 * 
 */
using MultiChFrameSampsQueue_t = ThreadSafeQueue<MultiChFrameSamps_t>;

/**
 * @brief Symbols of all channels belonging to one frame 
 * 
 */
using MultiChFrameSyms_t = std::vector<std::vector<Symbol_t>>;         

/**
 * @brief Thread-Safe Queue structure for Frame Symbols
 * 
 */
using MultiChFrameSymsQueue_t = ThreadSafeQueue<MultiChFrameSyms_t>;

/**
 * @brief Phase correction structure to store phase adjustments for NCOs
 * 
 */
struct Phase_t {
    float phi;               // Phase data
    unsigned int channel;    // Channel index
};

/**
 * @brief Thread-Safe Queue structure for Phase correction values 
 * 
 */
using PhaseQueue_t = ThreadSafeQueue<Phase_t>;

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
        using MsParams = MultiSync<synchronizer_iface>::ParamsType;
        using Callback_t = MultiSync<synchronizer_iface>::CallbackType;

        SyncWorker(const MsParams&   synchronizer_params,
                    std::atomic<bool>&          stop_signal_ref):
                        MultithreadWorker(stop_signal_ref),
                        ms_(num_channels, synchronizer_params, reinterpret_cast<Callback_t>(callback), &userdata_) {
                            frame_samps_queue_ = nullptr;
                            frame_syms_queue_ = nullptr; 
                            cb_data_.ms_ptr = &ms_; 
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
            frame_samps_queue_ = &queue;
            AddWorkerQueue<FrameSampsQueue_t>(frame_samps_queue_);
        };

        /**
         * @brief Add queue to retrieve symbol-blocks belonging to detected frames (Sync-Worker output)
         * 
         * @param queue Queue to retrieve symbol-blocks belonging to detected frames
         * @return * void 
         */
        void AddFrameSymsQueue(FrameSymsQueue_t& queue) {
            frame_syms_queue_ = &queue;
            AddWorkerQueue<FrameSymsQueue_t>(frame_syms_queue_);
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
         * @brief The Execute function continuously retrieves timestamped samples from the channel-specific rx-queues as rx-blocks and 
         * executes the synchronization algorithm on these samples through the MultiSync instance. 
         * When a frame is detected by a channel’s synchronizer, the resulting sample-block and associated callback data are pushed into 
         * thread-safe queues for use in subsequent processing stages. 
         * The queued data is tagged with the timestamp and the channel number of the synchronized sample-block, in which the frame was detected.
         * 
         * Throughout the synchronization process, phase corrections can be applied to the NCOs of the MultiSync instance to compensate 
         * the phase errors introduced by the hardware instances.
         * 
         * This function is executed within a dedicated thread.
         * 
         * @param stop_signal_called Stop signal to terminate the thread
         */
        void Execute() override final {
            std::vector<SampleBlock_t> rx_blocks;               // Blocks of samples retrieved from a channels rx-queue                   
            unsigned int i, j, num_written;

            while (!stop_signal_called->load()) {

                // Process Phase Correction to adjust NCO phase for the channel
                Phase_t phi_corr;
                if (PopItemFromQueue<Phase_t>(phi_corr_queue_, phi_corr)){
                    std::cout<< "Adjusted NCO of CH"<< phi_corr.channel<<" from"<< ms_.GetNcoPhase(phi_corr.channel)<<" rad";
                    ms_.AdjustNcoPhase(phi_corr.channel, phi_corr.phi);  // Adjust NCO phase for the channel
                    std::cout<< "to "<< ms_.GetNcoPhase(phi_corr.channel)<<" rad!"<<std::endl;
                };

                // Process channels
                for (i = 0; i < num_channels; ++i) {
                        // Clear samples and Callback-data
                        rx_blocks.clear();

                        // Process channel queue 
                        if (0 == PopBatchFromQueue<SampleBlock_t>(rx_queues_[i], rx_blocks))
                            continue;   // No samples available, skip channel

                        // Detect Packets 
                        for (j = 0; j < rx_blocks.size(); ++j) {
                            DetectFrame(rx_blocks[j], i);
                        }; 
                }; // for i num_channels
                
                // Push Frame-Samples to queue              
                if (frame_samps_queue_ && !frame_samps_.empty()){ 
                    PushBatchToQueue<FrameSamps_t>(*frame_samps_queue_, frame_samps_);
                };

                // Push Frame-Symbols to queue
                if (frame_syms_queue_ && !frame_syms_.empty()){ 
                    PushBatchToQueue<FrameSyms_t>(*frame_syms_queue_, frame_syms_);
                };

            } // while 
        };

    private:

        /**
         * @brief Callback data structure for userdata shared with the synchronizers in Callback-function
         * 
         */
        struct CallbackData_t {
            FrameSamps_t frame_samps;                   // Samples belonging to detected frames
            FrameSyms_t frame_syms;                  // Symbols belonging to detected frames
            MultiSync<synchronizer_iface>* ms_ptr;      // Pointer to MultiSync instance
            unsigned int channel;                       // Channel index
            bool frame_detected;                        // Frame detected indicator
        };

        /**
         * @brief Callback-data buffer shared by each channel-synchronizers callback-function 
         * 
         */
        CallbackData_t cb_data_;

        /**
         * @brief Array of pointers to Callback-data
         * 
         */
        void* userdata_;

        /**
         * @brief Internal MultiSync instance
         * 
         */
        MultiSync<synchronizer_iface> ms_;

        /**
         * @brief Vector of frame sample blocks detected in the last Execute cycle
         * 
         */
        std::vector<FrameSamps_t> frame_samps_;

        /**
         * @brief Vector of frame data-symbols blocks detected in the last Execute cycle
         * 
         */
        std::vector<FrameSyms_t> frame_syms_;

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
         * @brief Detect frames in the given rx-sample block for the specified channel. 
         * The Frame samples and -symbols are stored in the internal cb_data_ structure.
         * 
         * @param rx_block Block of received samples with timestamp
         * @param ch Channel index
         * @return true Frame detected
         * @return false No frame detected
         */
        void DetectFrame(SampleBlock_t& rx_block, unsigned int ch){ 
                // Clear callback-data buffer
                cb_data_.frame_samps.samples.clear();  
                cb_data_.frame_syms.symbols.clear();      
                cb_data_.frame_detected = false;          
                cb_data_.channel = ch; 

                // Execute Synchronizer 
                ms_.Execute(ch, &rx_block.samples);          
                    
                // Process callback-data 
                if (cb_data_.frame_detected) {    
                        cb_data_.frame_samps.timestamp = rx_block.timestamp; 
                        cb_data_.frame_syms.timestamp = rx_block.timestamp;      
                        cb_data_.frame_samps.channel = ch; 
                        cb_data_.frame_syms.channel = ch;  
                        frame_samps_.push_back(cb_data_.frame_samps);    
                        frame_syms_.push_back(cb_data_.frame_syms);          
                };
        };

        /**
         * @brief ofdmframesync callback function to push received symbols from synchronizer to the cb-data-queue
         * 
         * @param _X array of received subcarrier samples [size: _M x 1]
         * @param _p subcarrier allocation array [size: _M x 1]
         * @param _M number of subcarriers
         * @param _cb_data pointer to internal cb-data structure
         * @return return 1 to reset synchronizer after first data symbol
         */
        static int callback(std::complex<float>* _X, unsigned char * _p, unsigned int _M, void * _cb_data){
            // Set detected frame indicator
            CallbackData_t& cb = *static_cast<CallbackData_t*>(_cb_data);
            cb.frame_detected = true;

            // Add samples to callback-data buffer
            cb.ms_ptr->GetFrameSamps(cb.channel, &cb.frame_samps.samples);   

            // Add symbols from all subcarriers to callback-data buffer 
            for (unsigned int i = 0; i < _M; ++i) {
                // ignore 'null' and 'pilot' subcarriers
                if (_p[i] != OFDMFRAME_SCTYPE_DATA)
                    continue;
                // Add data symbol to callback-data buffer
                cb.frame_syms.symbols.push_back(_X[i]);  
            }
            // Reset synchronizer after returning the first data symbol (return 1)
            return 1;
        };

};

#endif // SYNCWORKER_H