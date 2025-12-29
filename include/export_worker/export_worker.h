/**
 * @file export_worker.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2025-07-11
 * 
 * 
 */

#ifndef EXPORT_WORKER_H
#define EXPORT_WORKER_H

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

#include <multisync/multisync.h>
#include <zmq_socket/zmq_socket.h>
#include <matlab_export/matlab_export.h>

/**
 * @brief The cfr_export_worker first identifies queued CFRs whose timestamps all fall within a specified time range 
 * and whose channel numbers are all unique within that group. 
 * Grouping minimizes the number of forwarded CFRs that correspond to frames not detected across all channels. 
 * Once a complete group is identified, the function exports the group simultaneously through the referenced ZmqSender and MatlabExport instances. 
 * The function includes buffer management to maintain efficiency by removing processed CFRss without valid groups while retaining a minimum number of entries per channel. 
 * At termination, it adds plotting commands to the MatlabExport instance to visualize magnitude, phase, and complex representation of the exported CFRs. 
 * 
 * This function is executed within a dedicated thread.
 * 
 * @tparam num_channels Number of Channels CFRs belong to
 * @param cfr_queue Reference to the thread-safe queue storing the detected CFRs
 * @param max_age Time-range within which the timestamps of a CFRs corresponding to one frame must fall
 * @param sender Reference to the ZmqSender instance for exporting the CFR groups
 * @param m_file Reference to the MatlabExport instance for exporting the CFR groups
 * @param stop_signal_called Stop signal to terminate the thread
 */
template <std::size_t num_channels>
void cfr_export_worker( CfrQueue_t& cfr_queue, 
                    uhd::time_spec_t max_age,
                    ZmqSender& sender,
                    MatlabExport& m_file,
                    std::atomic<bool>& stop_signal_called) {

    // Queued CFRs of all channels and all times 
    std::vector<Cfr_t> cfr_buffer;         

    // Sorted and time-matched CFRs of all channels
    std::vector<std::vector<std::complex<float>>> cfr_group(num_channels);

    while (!stop_signal_called.load()) {
        // Move cfr queue to buffer
        std::unique_lock<std::mutex> lock_cfr(cfr_queue.mtx);
        cfr_queue.cv.wait(lock_cfr, [&cfr_queue, &stop_signal_called] { 
            return !cfr_queue.queue.empty() || stop_signal_called.load(); 
        });

        if (!cfr_queue.queue.empty()) {
            cfr_buffer.push_back(std::move(cfr_queue.queue.front()));
            cfr_queue.queue.pop();
        }

        // Sort CFRs by timestamp
        std::sort(cfr_buffer.begin(), cfr_buffer.end(),
            [](const Cfr_t& a, const Cfr_t& b) {
                return a.timestamp < b.timestamp;
            });

        // Find a group of CFRs from all channels within the max_age window
        unsigned int i, j;
        for (i = 0; i < cfr_buffer.size(); ++i) {
            std::vector<const Cfr_t*> group(num_channels, nullptr); // Group of CFRs from each channel
            const auto& base = cfr_buffer[i];                       // Add initial CFR to group
            group[base.channel] = &base;

            // Find all CFRs around base within the max_age window
            for (j = i + 1; j < cfr_buffer.size(); ++j) {
                // Next timestamp out of range -> no group existing for this base-CFR
                if ((cfr_buffer[j].timestamp - base.timestamp) > max_age)
                    break;
                // Found CFR for another channel within the max_age window
                if (!group[cfr_buffer[j].channel])
                    group[cfr_buffer[j].channel] = &cfr_buffer[j];
            }

            // Check if group is complete (all channels have a CFR)
            bool complete = true;
            for (auto ptr : group) {
                if (!ptr) { complete = false; break; }
            }

            // Export if a complete group was found
            if (complete) {
                // Reset the CFR group
                cfr_group = std::vector<std::vector<std::complex<float>>>(num_channels);
                // Prepare CFRs sorted by channel
                for (const auto& cfr : group) {
                    cfr_group[cfr->channel] = cfr->cfr;;
                }

                // ZMQ Export
                sender.send(cfr_group);

                // MATLAB Export 
                std::cout << "Exported CFR at timestamps ";
                for (const auto& cfr : group) {
                    std::string timestamp = std::to_string(cfr->timestamp.get_full_secs())+std::to_string(cfr->timestamp.get_tick_count(1000));
                    std::cout << "CH"<<cfr->channel<<": "<<timestamp << " ";
                    m_file.Add(cfr->cfr, "CH" + std::to_string(cfr->channel) +"_"+timestamp);
                }
                std::cout <<"!"<< std::endl;

                // Clear the buffer up to the current index
                cfr_buffer.erase(cfr_buffer.begin(), cfr_buffer.begin()+j);

                // Break queue processing 
                break;
            }
        }

        // Clear all processed CFRs from the buffer, keep min. one CFR for each channel
        if (cfr_buffer.size() > num_channels){
            int rest = static_cast<int>(cfr_buffer.size()) - static_cast<int>(i) - 1;
            cfr_buffer.erase(cfr_buffer.begin(), cfr_buffer.end() - (rest>static_cast<int>(num_channels) ? rest : static_cast<int>(num_channels)));
        };
    }

    // Add combined plot-commands to the MATLAB file
    for (auto& varname : m_file.GetVarNames()) {
        std::stringstream matlab_cmd;
        matlab_cmd << "figure;";

        // CFR Magnitude
        matlab_cmd << "subplot(2,1,1); hold on;";
        matlab_cmd << "plot(abs("<< varname <<"), 'DisplayName', '" << varname << "');";
        matlab_cmd << "title('Channel Frequency Response Gain'); legend; grid on;";
        matlab_cmd << std::endl;

        // CFR Phase
        matlab_cmd << "subplot(2,1,2); hold on;";
        matlab_cmd << "plot(angle("<< varname <<"), 'DisplayName', '" << varname << "');";
        matlab_cmd << "title('Channel Frequency Response Phase'); legend; grid on;";
        matlab_cmd << std::endl;

        // CFR in Complex 
        matlab_cmd << "figure;";
        matlab_cmd << "plot(real("<< varname <<"), imag("<< varname
                <<"), '.', 'DisplayName', '"<< varname <<"'); hold on;";
        matlab_cmd << "title('CFR'); xlabel('Real'); ylabel('Imag'); axis equal; legend; grid on;";
        matlab_cmd << std::endl;

        // Add the complete command string to MATLAB export
        m_file.Add(matlab_cmd.str());
    }

}

/**
 * @brief cbdata_export_worker is responsible for forwarding the queued callback-data items to the associated MatlabExport instance.
 * Upon termination, the worker additionally generates the Matlab plotting commands required to visualize the constellation diagrams 
 * of all received frames and appends them to the MatlabExport instance.
 * 
 * This function is executed within a dedicated thread.
 * 
 * @param cbdata_queue Reference to thread-safe queue storing the callback-data items
 * @param m_file Reference to the MatlabExport instance for exporting the callback-data items
 * @param stop_signal_called Stop signal to terminate the thread
 */
void cbdata_export_worker(  CbDataQueue_t& cbdata_queue, 
                            MatlabExport& m_file,
                            std::atomic<bool>& stop_signal_called) {

    // Queued cb-data 
    std::vector<CallbackData_t> cbdata_buffer;    

    unsigned int i;
    while (!stop_signal_called.load()) {
        // Move CB-Data queue to buffer
        std::unique_lock<std::mutex> lock_cb(cbdata_queue.mtx);
        cbdata_queue.cv.wait(lock_cb, [&cbdata_queue, &stop_signal_called] { 
            return !cbdata_queue.queue.empty() || stop_signal_called.load(); 
        });

        if (stop_signal_called.load()) break;

        if (!cbdata_queue.queue.empty()) {
            cbdata_buffer.push_back(std::move(cbdata_queue.queue.front()));
            cbdata_queue.queue.pop();
        }

        // Export CB-Data buffer to MATLAB file
        for (unsigned int i = 0; i < cbdata_buffer.size(); ++i) {
            std::string timestamp = std::to_string(cbdata_buffer[i].timestamp.get_full_secs())+std::to_string(cbdata_buffer[i].timestamp.get_tick_count(1000));
            std::string suffix = "CH" + std::to_string(cbdata_buffer[i].channel)+"_"+ timestamp;
            m_file.Add(cbdata_buffer[i].buffer, suffix);
        }

        // Clear the buffer
        cbdata_buffer.clear();
    }

    // Add combined plot-command to the MATLAB file
    std::stringstream matlab_cmd;

    // CFR in Complex 
    matlab_cmd << "figure;";
    for (auto& varname : m_file.GetVarNames()) {
        matlab_cmd << "plot(real("<< varname <<"), imag("<< varname
                <<"), '.', 'DisplayName', '"<< varname <<"'); hold on;";
        matlab_cmd << std::endl;
    }
    matlab_cmd << "title('Received Data'); xlabel('Real'); ylabel('Imag'); axis equal; legend; grid on;";
    matlab_cmd << std::endl;

    // Add the complete command string to MATLAB export
    m_file.Add(matlab_cmd.str());
}


#endif // EXPORT_WORKER_H