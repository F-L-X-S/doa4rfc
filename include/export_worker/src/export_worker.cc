/**
 * @file export_worker.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2025-07-11
 * 
 * 
 */

#include <export_worker.h>

/**
 * @brief cbdata_export_worker is responsible for forwarding the queued callback-data items to the associated MatlabXport instance.
 * Upon termination, the worker additionally generates the Matlab plotting commands required to visualize the constellation diagrams 
 * of all received frames and appends them to the MatlabXport instance.
 * 
 * This function is executed within a dedicated thread.
 * 
 * @param cbdata_queue Reference to thread-safe queue storing the callback-data items
 * @param m_file Reference to the MatlabXport instance for exporting the callback-data items
 * @param stop_signal_called Stop signal to terminate the thread
 */
void cbdata_export_worker(  FrameSymsQueue_t& cbdata_queue, 
                            MatlabXport& m_file,
                            std::atomic<bool>& stop_signal_called) {

    // Queued cb-data 
    std::vector<FrameSyms_t> cbdata_buffer;    

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
            std::string timestamp = std::to_string(cbdata_buffer[i].timestamp/1e6);
            std::string suffix = "CH" + std::to_string(cbdata_buffer[i].channel)+"_"+ timestamp;
            m_file.Add(cbdata_buffer[i].symbols, suffix);
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