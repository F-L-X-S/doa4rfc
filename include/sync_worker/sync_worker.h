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

#ifndef SYNC_WORKER_H
#define SYNC_WORKER_H


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
    std::vector<Sample_t> samples;                    // Received samples
    uhd::time_spec_t timestamp;                         // Timestamp of the sample block
};

struct RxSamplesQueue_t
{
    std::queue<RxSampleBlock_t> queue;
    std::mutex mtx;
    std::condition_variable cv;
};

struct Cfr_t {
    std::vector<std::complex<float>> cfr;               // Channel Frequency Response
    uhd::time_spec_t  timestamp;                        // Timestamp of the CFR
    unsigned int channel;                               // Channel index
};

struct CfrQueue_t
{
    std::queue<Cfr_t> queue;
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
 * @brief The sync_worker function continuously retrieves timestamped sample-blocks from the channel-specific queues and 
 * executes the synchronization algorithm on these samples through the MultiSync instance. 
 * When a frame is detected by a channel’s synchronizer, the resulting CFR and associated callback data are pushed into 
 * thread-safe queues for use in subsequent processing stages. 
 * The queued data is tagged with the timestamp and the channel number of the synchronized sample-block, in which the frame was detected.
 * 
 * Throughout the synchronization process, phase corrections can be applied to the NCOs of the MultiSync instance to compensate 
 * the phase errors introduced by the hardware instances.
 * 
 * This function is executed within a dedicated thread.
 * 
 * @tparam num_channels Number of Channels to synchronize
 * @tparam syncronizer_type Type of the synchronizer to use (e.g. ofdmframesync)
 * @tparam cb_data_type Callback-data type to use 
 * @param ms Reference to the MultiSync instance
 * @param cb_data Reference to the array of callback-data structures (one for each channel)
 * @param rx_queues Reference to the array of thread-safe queues storing received sample blocks (one for each channel)
 * @param cfr_queue Reference to the thread-safe queue to push the detected CFRs
 * @param cbdata_queue Reference to the thread-safe queue to push the Callback-data
 * @param phi_error_queue Reference to the thread-safe queue to receive phase corrections for the NCOs
 * @param stop_signal_called Stop signal to terminate the thread
 */
template <std::size_t num_channels, typename syncronizer_type, typename cb_data_type>
void sync_worker(   syncronizer_type& ms,
                    std::array<CallbackData_t, num_channels>& cb_data,
                    std::array<RxSamplesQueue_t, num_channels>& rx_queues,
                    CfrQueue_t& cfr_queue,
                    CbDataQueue_t& cbdata_queue,
                    PhaseQueue_t& phi_error_queue,
                    std::atomic<bool>& stop_signal_called
                ) {

    std::vector<RxSampleBlock_t> sample_blocks;
    std::vector<std::complex<float>> rx_sample(1);
    Cfr_t cfr;                                                        
    unsigned int i, j, num_written;

    while (!stop_signal_called.load()) {
        // Process Phase Error queue to adjust NCO phase for the channel
        std::unique_lock<std::mutex> lock_phi(phi_error_queue.mtx);
        phi_error_queue.cv.wait_for(lock_phi, std::chrono::milliseconds(100), [&]() {
            return !phi_error_queue.queue.empty() || stop_signal_called.load();
        });

        while (!phi_error_queue.queue.empty()) {
            Phase_t phi_error = std::move(phi_error_queue.queue.front());
            std::cout<< "Adjusted NCO of CH"<< phi_error.channel<<" from"<< ms.GetNcoPhase(phi_error.channel)<<" rad";
            ms.AdjustNcoPhase(phi_error.channel, phi_error.phi);  // Adjust NCO phase for the channel
            std::cout<< "to "<< ms.GetNcoPhase(phi_error.channel)<<" rad!"<<std::endl;
            phi_error_queue.queue.pop();
        }
        lock_phi.unlock();

        // Process channels
        for (i = 0; i < num_channels; ++i) {
                // Clear samples and Callback-data
                sample_blocks.clear();
                cb_data[i].buffer.clear();  

                // Process channel queue 
                std::unique_lock<std::mutex> lock_rx(rx_queues[i].mtx);
                rx_queues[i].cv.wait(lock_rx, [&rx_queues, i, &stop_signal_called] { 
                    return !rx_queues[i].queue.empty() || stop_signal_called.load(); 
                });

                while (!rx_queues[i].queue.empty()) {
                    sample_blocks.push_back(std::move(rx_queues[i].queue.front()));
                    rx_queues[i].queue.pop();
                }

                // Detect Packets 
                for (j = 0; j < sample_blocks.size(); ++j) {
                        // Process all Samples in Block 
                        for (unsigned int k = 0; k < sample_blocks[j].samples.size(); ++k) { 

                            // Execute Synchronizer for channel i 
                            rx_sample[0]= sample_blocks[j].samples[k];
                            ms.Execute(i, &rx_sample);          
                                
                            // Check, if callback-data was updated by synchronizer
                            if (cb_data[i].buffer.size()){     
                                // Push CFR to queue                    
                                ms.GetCfr(i, &cfr.cfr);                                                 // Write cfr to callback data
                                cfr.timestamp = sample_blocks[j].timestamp;                             // Update timestamp
                                cfr.channel = i;                                                        // Set channel index
                                {
                                    std::lock_guard<std::mutex> lock_cfr(cfr_queue.mtx);
                                    cfr_queue.queue.push(std::move(cfr));
                                }
                                cfr_queue.cv.notify_one();

                                // Push Callback-data to queue
                                cb_data[i].timestamp = sample_blocks[j].timestamp;                      // Update timestamp
                                cb_data[i].channel = i;                                                 // Set channel index
                                {
                                    std::lock_guard<std::mutex> lock_cb(cbdata_queue.mtx);
                                    cbdata_queue.queue.push(std::move(cb_data[i]));
                                }
                                cbdata_queue.cv.notify_one();

                                // Print debug info 
                                std::cout << "Captured CFR for channel "<< i <<" at timestamp "<< cfr.timestamp.get_full_secs() << std::endl;
                            };
                        };
                    };
                };
    }
}

#endif // SYNC_WORKER_H