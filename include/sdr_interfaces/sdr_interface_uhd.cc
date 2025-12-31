/**
 * @file sdr_interface_uhd.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2025-12-29
 * 
 * 
 */

#include <sdr_interfaces/sdr_interface_uhd.h>

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
                std::atomic<bool>& stop_signal_called){

    uhd::tx_metadata_t md;
    std::vector<std::complex<float>*> buffs(1, &buff.front());
    size_t samples_sent=0;
    while (!stop_signal_called.load()) {
        const size_t n_tx = tx_stream->send(buffs, buff.size(), md);
        samples_sent+=n_tx;
        md.start_of_burst = false;
        md.has_time_spec  = false;

        //sleep after transmitting buffer 
        if (samples_sent>=buff.size()){
            samples_sent = 0;
            boost::this_thread::sleep(boost::posix_time::milliseconds(cycle_time));
        };
    }
}
