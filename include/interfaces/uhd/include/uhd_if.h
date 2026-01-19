/**
 * @file uhd_if.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2025-12-29
 * 
 * 
 */

#ifndef UHD_IF
#define UHD_IF

#include <uhd/usrp/multi_usrp.hpp>    
              
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

/**
 * @brief The stream_worker function references the UHD USRP interfaces, configuring all available instances simultaneously, 
 * with one designated as the master device for the MIMO configuration. Sample streaming to or from the USRP devices is accomplished 
 * by issuing stream commands. To prevent synchronization issues arising from variable program execution times, 
 * timed stream commands based on the device time are constructed and dispatched to the USRPs. 
 * For stable timing synchronization, the stream command that initiates continuous streaming is constructed and re-issued 
 * after a defined cycle time by the stream_worker function.
 * 
 * This function is executed within a separated thread.
 * 
 * @tparam num_channels 
 * @param usrps Reference to array with UHD USRP interfaces
 * @param max_samps Max. number of samples to receive per frame from the USRP
 * @param tx_rate TX Sample Rate [Sps]
 * @param rx_rate RX Sample Rate [Sps]
 * @param center_freq Center Frequency [Hz]
 * @param cycle_time Execution cycle time (Stream restart after 10 cycles) [ms]
 * @param stop_signal_called Stop signal to terminate the thread
 */
template <std::size_t num_channels>
void stream_worker( std::array<uhd::usrp::multi_usrp::sptr, num_channels>& usrps,
                    size_t& max_samps, 
                    double& tx_rate,
                    double& rx_rate,
                    double& center_freq,
                    unsigned int cycle_time,
                    std::atomic<bool>& stop_signal_called) {


    // Lock mboard clocks
    unsigned int i;
    for (i=0; i < num_channels; ++i){
        std::string clk_src = i==0 ? "internal":"mimo";
        usrps[i]->set_clock_source(clk_src, 0);                         // internal clock source for device 0 / mimo for other devices 
        if (i>0) usrps[i]->set_time_source(clk_src, 0);                 // mimo time source for devices > 0 
        if (i==0) usrps[i]->set_time_now(uhd::time_spec_t(0.0), 0);     // initialize device time
    };

    //sleep a while the slaves lock its time to the master
    boost::this_thread::sleep(boost::posix_time::milliseconds(cycle_time));

    // Configure tune request for desired center frequency 
    uhd::tune_request_t tune_request(center_freq); 
    tune_request.rf_freq_policy = uhd::tune_request_t::policy_t::POLICY_AUTO;
    std::cout << boost::format("Tune Policy: %f") % (tune_request.rf_freq_policy) << std::endl;

    for (auto usrp : usrps){
        //always select the subdevice first, the channel mapping affects the other settings
        usrp->set_rx_subdev_spec(uhd::usrp::subdev_spec_t("A:0"), 0);           //set the device 0 to use the A RX frontend (RX channel 0)
        usrp->set_tx_subdev_spec(uhd::usrp::subdev_spec_t("A:0"), 0);           //set the device 0 to use the A TX frontend (TX channel 0)
        usrp->set_rx_rate(rx_rate, uhd::usrp::multi_usrp::ALL_MBOARDS);       // set RX sample rate
        usrp->set_tx_rate(tx_rate, uhd::usrp::multi_usrp::ALL_MBOARDS);       // set TX sample rate
        usrp->set_rx_freq(tune_request, 0);                                     // set RX Frequency  
        usrp->set_tx_freq(tune_request, 0);                                     // set TX Frequency  
        usrp->set_rx_gain(30, 0);                                               // set the RX gain
        usrp->set_tx_gain(15, 0);                                               // set the TX gain
        usrp->set_rx_antenna("RX2", 0);                                         // set the RX antenna
        usrp->set_tx_antenna("TX/RX", 0);                                       // set the TX antenna
    }

    //sleep a while...
    boost::this_thread::sleep(boost::posix_time::milliseconds(cycle_time));

    // Print device configuration 
    for (i=0; i < num_channels; ++i){
        std::cout << boost::format("---- Configuration  Device %1% ----") % i << std::endl;
        std::cout << boost::format("Clock-src: %s") % usrps[i]->get_clock_source(0) << std::endl;
        std::cout << boost::format("Time-src: %s") % usrps[i]->get_time_source(0) << std::endl;

        std::cout << boost::format("TX Configuration:") << std::endl;
        std::cout << boost::format("\tRequired TX Rate: %f Msps...") % (tx_rate / 1e6) << std::endl;
        std::cout << boost::format("\tTX Rate: %f Msps...") % (usrps[i]->get_tx_rate(0) / 1e6) << std::endl;
        std::cout << boost::format("\tRequired TX Freq: %f MHz...") % (center_freq / 1e6) << std::endl;
        std::cout << boost::format("\tTX Freq: %f MHz...") % (usrps[i]->get_tx_freq(0) / 1e6) << std::endl;
        std::cout << boost::format("\tTX Gain: %f dB...") % usrps[i]->get_tx_gain(0) << std::endl;
        std::cout << boost::format("\tTX Bandwidth: %f MHz...") % (usrps[i]->get_tx_bandwidth(0) / 1e6) << std::endl;

        std::cout << boost::format("RX Configuration:") << std::endl;
        std::cout << boost::format("\tRequired RX Rate: %f Msps...") % (rx_rate / 1e6) << std::endl;
        std::cout << boost::format("\tRX Rate: %f Msps...") % (usrps[i]->get_rx_rate(0) / 1e6) << std::endl;
        std::cout << boost::format("\tRequired RX Freq: %f MHz...") % (center_freq / 1e6) << std::endl;
        std::cout << boost::format("\tRX Freq: %f MHz...") % (usrps[i]->get_rx_freq(0) / 1e6) << std::endl;
        std::cout << boost::format("\tRX Gain: %f dB...") % usrps[i]->get_rx_gain(0) << std::endl;
        std::cout << boost::format("\tRX Bandwidth: %f MHz...") % (usrps[i]->get_rx_bandwidth(0) / 1e6) << std::endl << std::endl;
    };

    // Configure stream command
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    stream_cmd.num_samps = max_samps; // number of samples to receive per frame
    stream_cmd.stream_now = false;  
    double seconds_in_future = 0.1; 

    // Cyclic burst stream
    while (!stop_signal_called.load()) {
            for (auto usrp : usrps){
            // Set the time spec to start receiving in the future
            stream_cmd.time_spec = usrp->get_time_now() + uhd::time_spec_t(seconds_in_future);  
            // Start USRPs streaming
            usrp->issue_stream_cmd(stream_cmd); 
            };

            // receive for cycle time 
            boost::this_thread::sleep(boost::posix_time::milliseconds(10*cycle_time));

            // Stop streaming 
            for (auto usrp : usrps){
                usrp->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
            }

            // stop for cycle time -> synchronization
            boost::this_thread::sleep(boost::posix_time::milliseconds((unsigned int) (0.1*cycle_time)));

    };

    // Stop streaming 
    for (auto usrp : usrps){
        usrp->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
    }

}

/**
 * @brief tx_worker reads the referenced buffer at the specified cycle time and forwards the data 
 * to the streamer for transmission. The buffer contains the baseband time‑domain sequence to be transmitted 
 * in the next cycle.
 * 
 * This function is executed within a dedicated thread for each uhd::tx\_streamer::sptr instance.
 * 
 * @param tx_stream Reference to the TX streamer interface
 * @param buff Baseband time‑domain sequence to be transmitted
 * @param cycle_time Cycle time to read and transmit the buffer [ms]
 * @param stop_signal_called Stop signal to terminate the thread
 */
void tx_worker(uhd::tx_streamer::sptr tx_stream,
                std::vector<Sample_t>& buff,
                unsigned int cycle_time,
                std::atomic<bool>& stop_signal_called);

/**
 * @brief rx_worker reads the complex samples from the referenced uhd::rx\_streamer::sptr instance and 
 * forwards each cycle's batch of samples as a block to a dedicated queue. To enable precise frame timing identification, 
 * the rx_worker attaches the USRP device time to the corresponding sample block. This timestamp is supplied by the 
 * streamer instance as metadata for each packet received from the USRP. The performance of this function is critical, 
 * as insufficient processing efficiency may result in overflow errors of the UHD driver.
 * 
 * This function is executed within a dedicated thread for each uhd::rx\_streamer::sptr instance.
 * 
 * @tparam buffer_size Max. number of samples to receive per frame from the USRP
 * @param rx_stream Reference to the RX streamer interface
 * @param q Reference to the thread-safe queue to push the received sample blocks
 * @param stop_signal_called Stop signal to terminate the thread
 */
template <std::size_t buffer_size>
void rx_worker( uhd::rx_streamer::sptr rx_stream,
                SampleBlockQueue_t& q,
                std::atomic<bool>& stop_signal_called) {
    uhd::rx_metadata_t md;
    std::vector<Sample_t> buff(buffer_size);

    while (!stop_signal_called.load()) {
        size_t n_rx = rx_stream->recv(&buff.front(), buff.size(), md, 1.0);
        SampleBlock_t sample_block;
        sample_block.samples.assign(buff.begin(), buff.begin() + n_rx);
        sample_block.timestamp = md.time_spec.to_ticks(1e9); // Convert to nanoseconds
        {
            std::lock_guard<std::mutex> lock(q.mtx);
            q.queue.push(std::move(sample_block));
        }
        q.cv.notify_one();
    }
}

#endif // UHD_IF