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

#include <uhd/usrp/multi_usrp.hpp>   

#include <multisync/multisync.h>

// Typedefs
using Sample_t = std::complex<float>;   // Received samples type

struct RxSampleBlock_t
{
    std::vector<Sample_t> samples;                      // Received samples
    uhd::time_spec_t timestamp;                         // Timestamp of the sample block
};

struct RxSamplesQueue_t
{
    std::queue<RxSampleBlock_t> queue;
    std::mutex mtx;
    std::condition_variable cv;
};

struct FrameSamps_t {
    std::vector<std::complex<float>> frame_samps;       // Baseband-Samples of the Frame 
    uhd::time_spec_t  timestamp;                        // Timestamp of the Frame 
    unsigned int channel;                               // Channel index
};

struct FrameSampsQueue_t
{
    std::queue<FrameSamps_t> queue;
    std::mutex mtx;
    std::condition_variable cv;
};

struct CallbackData_t {
    std::vector<std::complex<float>> buffer;            // Buffer to store detected symbols 
    uhd::time_spec_t  timestamp;                        // Timestamp 
    unsigned int channel;                               // Channel index
};

struct CbDataQueue_t
{
    std::queue<CallbackData_t> queue;
    std::mutex mtx;
    std::condition_variable cv;
};

struct Phase_t {
    float phi;               // Phase data
    unsigned int channel;    // Channel index
};

struct PhaseQueue_t
{
    std::queue<Phase_t> queue;
    std::mutex mtx;
    std::condition_variable cv;
};

/**
 * @brief callback function to push received symbols from synchronizer to the cb-data-queue
 * 
 * @param _X array of received subcarrier samples [size: _M x 1]
 * @param _p subcarrier allocation array [size: _M x 1]
 * @param _M number of subcarriers
 * @param _cb_data user-defined data pointer
 * @return int 
 */
int callback(std::complex<float>* _X, unsigned char * _p, unsigned int _M, void * _cb_data);

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
 * This function is executed within a dedicated thread.
 * 
 * @tparam num_channels Number of Channels to synchronize
 * @tparam synchronizer_iface Type of the synchronizer interface to use (e.g. ofdmframesync_iface)
 * @param stop_signal_called Stop signal to terminate the thread
 */
template <std::size_t num_channels, typename synchronizer_iface>
class SyncWorker {
    public:
        using MsParams = MultiSync<synchronizer_iface>::ParamsType;

        SyncWorker(const MsParams&   synchronizer_params,
                    std::atomic<bool>&          stop_signal_ref):
                        stop_signal_called(&stop_signal_ref),
                        ms(num_channels, synchronizer_params, callback, userdata) {
                            frame_samps_queue = nullptr;
                            cbdata_queue = nullptr;
                            sync_thread_ = nullptr;

                            // Initialize Array of Pointers to CB-Data 
                            for (unsigned int i = 0; i < num_channels; ++i)
                                userdata[i] = &cb_data[i];
        };

        ~SyncWorker(){
            StopSyncWorker();
        };

        /**
         * @brief Add queue to retrieve sample-blocks belonging to detected frames (Sync-Worker output)
         * 
         * @param queue Queue to retrieve sample-blocks belonging to detected frames
         * @return * void 
         */
        void AddFrameSampsQueue(FrameSampsQueue_t& queue) {
            frame_samps_queue = &queue;
        };

        /**
         * @brief Add queue to retrieve callback-data belonging to detected frames (Sync-Worker output)
         * 
         * @param queue Queue to retrieve callback-data belonging to detected frames
         */
        void AddCbDataQueue(CbDataQueue_t& queue) {
            cbdata_queue = &queue;
        };

        /**
         * @brief Get the reference to the internal rx-sample queues (one for each channel)
         * 
         * @return std::array<RxSamplesQueue_t, num_channels>* Reference to an array of rx-sample queues 
         */
        std::array<RxSamplesQueue_t, num_channels>* GetRxQueues() {
            return &rx_queues;
        };

        /**
         * @brief Get the reference to a internal rx-sample queue for a specific channel
         * 
         * @return RxSamplesQueue_t*  Reference to channel-specific rx-sample queue
         */
        RxSamplesQueue_t* GetRxQueue(int channel) {
            return &rx_queues[channel];
        };

        /**
         * @brief Get the reference to a internal Phase-Corr Queue object. 
         * Phase-corrections pushed to the queue are applied by the sync-worker to correct channel specific phase-offsets.
         * 
         * @return PhaseQueue_t& reference to internal Phase-Corr Queue 
         */
        PhaseQueue_t* GetPhaseCorrQueue() {
            return &phi_corr_queue;
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
        void Execute() {
            std::vector<RxSampleBlock_t> rx_blocks;             // Block of samples retrieved from a channels rx-queue
            std::vector<std::complex<float>> rx_sample(1);      // sample from rx-block to be processed 
            FrameSamps_t frame_samps;                           // Block of samples belonging to a detected frame                         
            unsigned int i, j, num_written;

            while (!stop_signal_called->load()) {
                // Process Phase Error queue to adjust NCO phase for the channel
                std::unique_lock<std::mutex> lock_phi(phi_corr_queue.mtx);
                phi_corr_queue.cv.wait_for(lock_phi, std::chrono::milliseconds(100), [&]() {
                    return !phi_corr_queue.queue.empty() || stop_signal_called->load();
                });

                while (!phi_corr_queue.queue.empty()) {
                    Phase_t phi_error = std::move(phi_corr_queue.queue.front());
                    std::cout<< "Adjusted NCO of CH"<< phi_error.channel<<" from"<< ms.GetNcoPhase(phi_error.channel)<<" rad";
                    ms.AdjustNcoPhase(phi_error.channel, phi_error.phi);  // Adjust NCO phase for the channel
                    std::cout<< "to "<< ms.GetNcoPhase(phi_error.channel)<<" rad!"<<std::endl;
                    phi_corr_queue.queue.pop();
                }
                lock_phi.unlock();

                // Process channels
                for (i = 0; i < num_channels; ++i) {
                        // Clear samples and Callback-data
                        rx_blocks.clear();
                        cb_data[i].buffer.clear();  

                        // Process channel queue 
                        std::unique_lock<std::mutex> lock_rx(rx_queues[i].mtx);
                        rx_queues[i].cv.wait(lock_rx, [this, i] { 
                            return !this->rx_queues[i].queue.empty() || this->stop_signal_called->load(); 
                        });

                        while (!rx_queues[i].queue.empty()) {
                            rx_blocks.push_back(std::move(rx_queues[i].queue.front()));
                            rx_queues[i].queue.pop();
                        }

                        // Detect Packets 
                        for (j = 0; j < rx_blocks.size(); ++j) {
                                // Process all Samples in Block 
                                for (unsigned int k = 0; k < rx_blocks[j].samples.size(); ++k) { 

                                    // Execute Synchronizer for channel i 
                                    rx_sample[0]= rx_blocks[j].samples[k];
                                    ms.Execute(i, &rx_sample);          
                                        
                                    // Check, if callback-data was updated by synchronizer
                                    if (cb_data[i].buffer.size()){     
                                        // Push Frame-Samples to queue              
                                        if (frame_samps_queue){ 
                                            ms.GetFrameSamps(i, &frame_samps.frame_samps);                          // Write frame-samples to callback data
                                            frame_samps.timestamp = rx_blocks[j].timestamp;                     // Update timestamp
                                            frame_samps.channel = i;                                                // Set channel index
                                            {
                                                std::lock_guard<std::mutex> lock_frame_samps(frame_samps_queue->mtx);
                                                frame_samps_queue->queue.push(std::move(frame_samps));
                                            }
                                            frame_samps_queue->cv.notify_one();
                                        };
                                        // Push Callback-data to queue
                                        if (cbdata_queue){ 
                                            cb_data[i].timestamp = rx_blocks[j].timestamp;                      // Update timestamp
                                            cb_data[i].channel = i;                                                 // Set channel index
                                            {
                                                std::lock_guard<std::mutex> lock_cb(cbdata_queue->mtx);
                                                cbdata_queue->queue.push(std::move(cb_data[i]));
                                            }
                                            cbdata_queue->cv.notify_one();
                                        };
                                        // Print debug info 
                                        std::cout << "Captured Frame for channel "<< i <<" at timestamp "<< frame_samps.timestamp.get_full_secs() << std::endl;
                                    };
                                };
                            };
                        };
            } // while 
        };

        /**
         * @brief Run Multi-channel synchronization within a separated thread
         * 
         */
        void RunSyncWorker() {
            if(!sync_thread_) sync_thread_ = new std::thread(&SyncWorker::Execute, this);
        }

        /**
         * @brief Stop thread executing multi-channel synchronization
         * 
         */
        void StopSyncWorker() {
            // Set stop signal 
            stop_signal_called->store(true);

            // Wait...
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            // Notify connected queues
            for (unsigned int i = 0; i < num_channels; ++i){
                rx_queues[0].cv.notify_all();
            };
            phi_corr_queue.cv.notify_all();
            if (cbdata_queue) cbdata_queue->cv.notify_all();
            if (frame_samps_queue) frame_samps_queue->cv.notify_all();

            // Kill thread
            if (sync_thread_){
                sync_thread_->join();
                delete sync_thread_;  
            };          
        }

    private:
        /**
         * @brief Internal MultiSync instance
         * 
         */
        MultiSync<synchronizer_iface> ms;

        /**
         * @brief Received samples queues for each channel
         * 
         */
        std::array<RxSamplesQueue_t, num_channels> rx_queues;

        /**
         * @brief Callback-data buffer for each channels's synchronizer 
         * 
         */
        std::array<CallbackData_t, num_channels> cb_data;

        /**
         * @brief Array of pointers to Callback-data
         * 
         */
        void* userdata[num_channels];

        /**
         * @brief Phase correction values queue to adjust NCO phases
         * 
         */
        PhaseQueue_t phi_corr_queue;

        /**
         * @brief External queue to push detected frame samples
         * 
         */
        FrameSampsQueue_t* frame_samps_queue;

        /**
         * @brief External queue to push callback-data of detected frames 
         * 
         */
        CbDataQueue_t* cbdata_queue;

        /**
         * @brief Reference to sync-worker thread
         * 
         */
        std::thread* sync_thread_;

        /**
         * @brief External stop signal to terminate the threads
         * 
         */
        std::atomic<bool>* stop_signal_called;

};

#endif // SYNCWORKER_H