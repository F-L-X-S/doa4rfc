/**
 * @file matlab_worker.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2025-07-11
 * 
 * 
 */

#include <matlab_if.h>


MatlabWorker::MatlabWorker(     MatlabXport& m_xport,
                                std::atomic<bool>& stop_signal_ref):
                                m_xport_(m_xport),
                                MultithreadWorker(stop_signal_ref)
{
    AddWorkerQueue<ThreadSafeQueue<Samples_2dim_t>>(&multich_samps_queue_);
    AddWorkerQueue<ThreadSafeQueue<Symbols_2dim_t>>(&multich_syms_queue_);
};

MatlabWorker::~MatlabWorker(){};

/**
 * @brief Get the reference to the internal input queue for multi-channel frame samples (timestamped sample vector for each channel belonging to one frame).
 * Dimensions: [channel, sample_index]
 * 
 * @return ThreadSafeQueue<samples_2dim_t>*  Reference to the internal multi-channel frame samples  queue
 */
ThreadSafeQueue<Samples_2dim_t>* MatlabWorker::GetMultiChSampsQueue() {
    return &multich_samps_queue_;
};

/**
 * @brief Get the reference to the internal input queue for multi-channel frame symbols (timestamped symbol vector for each channel belonging to one frame)
 * Dimensions: [channel, symbol_index]
 * 
 * @return ThreadSafeQueue<Symbols_2dim_t>*  Reference to the internal multi-channel frame symbols  queue
 */
ThreadSafeQueue<Symbols_2dim_t>* MatlabWorker::GetMultiChSymsQueue() {
    return &multich_syms_queue_;
};

void MatlabWorker::SetExportEnabled(bool enabled) {
    export_enabled_.store(enabled);
};

bool MatlabWorker::GetExportEnabled() const {
    return export_enabled_.load();
};

void MatlabWorker::ExportSingle() {
    export_enabled_.store(false);
    samps_single_shot_.store(1);
    syms_single_shot_.store(1);
};

/**
 * @brief This function is executed within a dedicated thread.
 *
 * @param stop_signal_called Stop signal to terminate the thread
 */
void MatlabWorker::Execute() {

    while (!stop_signal_called->load()) {
        // Non-blocking pop from queues to buffers
        std::vector<Samples_2dim_t> multich_samps_buffer;
        std::vector<Symbols_2dim_t> multich_syms_buffer;
        auto samps_count = PopBatchFromQueue(multich_samps_queue_, multich_samps_buffer, 0);
        auto syms_count = PopBatchFromQueue(multich_syms_queue_, multich_syms_buffer, 0);
        if (0 == samps_count && 0 == syms_count) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Avoid busy-waiting when no channel has data
            continue;   // No samples available
        }

        // Discard data if export is disabled and no single-shot is pending
        if (!export_enabled_.load() && samps_single_shot_.load() == 0 && syms_single_shot_.load() == 0)
            continue;

        // Export buffers to MATLAB file (only if enabled or single-shot active)
        // Single-shot flag is only consumed when the respective queue had data this iteration,
        // preventing the flag from being cleared while the buffer is still empty.
        if (export_enabled_.load() || (samps_single_shot_.load() > 0 && samps_count > 0)) {
            if (!export_enabled_.load() && samps_single_shot_.load() > 0) {
                // Single-shot: keep only the first frame
                if (multich_samps_buffer.size() > 1)
                    multich_samps_buffer.resize(1);
                samps_single_shot_.store(0);
            }
            ExportSampsBuffer(multich_samps_buffer);
        }

        if (export_enabled_.load() || (syms_single_shot_.load() > 0 && syms_count > 0)) {
            if (!export_enabled_.load() && syms_single_shot_.load() > 0) {
                // Single-shot: keep only the first frame
                if (multich_syms_buffer.size() > 1)
                    multich_syms_buffer.resize(1);
                syms_single_shot_.store(0);
            }
            ExportSymsBuffer(multich_syms_buffer);
        }

    } // while
};

/**
 * @brief Stop thread Execute  function and add plotting commands to the MatlabXport instance.
 * 
 */
void MatlabWorker::StopWorker() {
    AddSampsPlotCommand();
    AddSymsPlotCommand();
    MultithreadWorker::StopWorker();
};

/**
 * @brief Export all available multi-channel frame samples from the multich_samps_queue_ to the MatlabXport instance.
 *
 */
void MatlabWorker::ExportSampsBuffer(std::vector<Samples_2dim_t> multich_samps_buffer) {
    for (const auto& frame : multich_samps_buffer) {
        for (unsigned int ch = 0; ch < frame.size(); ++ch) {
            std::string suffix = "SAMPS_CH" + std::to_string(ch) + "_F" + std::to_string(samps_frame_counter_);
            m_xport_.Add(frame[ch], suffix);
        }
        ++samps_frame_counter_;
    }
};

/**
 * @brief Export all available multi-channel frame symbols from the multich_syms_queue_ to the MatlabXport instance.
 *
 */
void MatlabWorker::ExportSymsBuffer(std::vector<Symbols_2dim_t> multich_syms_buffer) {
    for (const auto& frame : multich_syms_buffer) {
        for (unsigned int ch = 0; ch < frame.size(); ++ch) {
            std::string suffix = "SYMS_CH" + std::to_string(ch) + "_F" + std::to_string(syms_frame_counter_);
            m_xport_.Add(frame[ch], suffix);
        }
        ++syms_frame_counter_;
    }
};

/**
 * @brief Add command to plot multi-channel frame samples to the MatlabXport Instance.
 *
 */
void MatlabWorker::AddSampsPlotCommand() {
    std::stringstream matlab_cmd;

    matlab_cmd << "figure;";
    for (auto& varname : m_xport_.GetVarNames()) {
        if (varname.find("SAMPS") == std::string::npos) continue;
        matlab_cmd << "subplot(2,1,1); hold on;";
        matlab_cmd << "plot(real("<< varname <<"), 'DisplayName', 'Re("<< varname <<")');";
        matlab_cmd << "plot(imag("<< varname <<"), 'DisplayName', 'Im("<< varname <<")');";
        matlab_cmd << std::endl;
        matlab_cmd << "subplot(2,1,2); hold on;";
        matlab_cmd << "plot(abs("<< varname <<"), 'DisplayName', '"<< varname <<"');";
        matlab_cmd << std::endl;
    }
    matlab_cmd << "subplot(2,1,1); title('Samples'); xlabel('Sample Index'); ylabel('Amplitude'); legend; grid on;";
    matlab_cmd << std::endl;
    matlab_cmd << "subplot(2,1,2); title('Samples Magnitude'); xlabel('Sample Index'); ylabel('Magnitude'); legend; grid on;";
    matlab_cmd << std::endl;

    m_xport_.Add(matlab_cmd.str());
};


/**
 * @brief Add command to plot multi-channel frame symbols to the MatlabXport Instance.
 *
 */
void MatlabWorker::AddSymsPlotCommand() {
    std::stringstream matlab_cmd;

    matlab_cmd << "figure;";
    for (auto& varname : m_xport_.GetVarNames()) {
        if (varname.find("SYMS") == std::string::npos) continue;
        matlab_cmd << "plot(real("<< varname <<"), imag("<< varname
                <<"), '.', 'DisplayName', '"<< varname <<"'); hold on;";
        matlab_cmd << std::endl;
    }
    matlab_cmd << "title('Received Symbols'); xlabel('Real'); ylabel('Imag'); axis equal; legend; grid on;";
    matlab_cmd << std::endl;

    m_xport_.Add(matlab_cmd.str());
};
