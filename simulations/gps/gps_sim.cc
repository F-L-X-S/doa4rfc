/**
 * @file gps_sim.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 *
 * @brief Reads a pre-generated GPS L1 C/A ULA dataset (binary file produced by
 * gps_ula_dataset_gen.m) and feeds the per-element baseband samples into the
 * doa4rfc synchronization pipeline via ZMQ, cycling through all frames.
 *
 * @version 0.1
 * @date 2026-07-06
 *
 */

 #include <iostream>
 #include <fstream>
 #include <csignal>
 #include <stdexcept>

 #include <liquid.h>
 #include <doa4rfc.h>
 #include <sc_standards.h>
 #include <sync_worker.h>
 #include <grouping_worker.h>
 #include <multithread_worker.h>
 #include <zmq_if.h>
 #include <matlab_if.h>
 #include <signal_generator.h>
 #include <ui_worker.h>

using namespace doa4rfc;

// Number of ULA channels — must match N_rx used in gps_ula_dataset_gen.m
#define NUM_CHANNELS 4

// Binary dataset file written by gps_ula_dataset_gen.m.
// Regenerate whenever N_rx, PRN, DOA or SNR settings change.
#define DATASET_FILE "simulations/gps/records/gps_l1ca_PRN1_N4_DOA30deg_SNR10dB.bin"

// ZMQ-socket for import of the generated baseband samples
#define IMPORT_INTERFACE "tcp://127.0.0.1:5554"

// ZMQ-socket for data export to MUSIC running in Python-application
#define EXPORT_INTERFACE "tcp://127.0.0.1:5555"

// Python-Application with MUSIC algorithm for DoA estimation
#define PYTHONPATH "./music/env/bin/python"
#define MUSIC_PYFILE "./music/music-spectrum.py"

// MATLAB output file to store results
#define M_FILE "simulations/gps/gps_sim.m"

// Signal handler to stop by keyboard interrupt
std::atomic<bool> stop_signal_called(false);
void sig_int_handler(int) {
    stop_signal_called = true;
}

// Reads binary dataset written by gps_ula_dataset_gen.m.
// Layout: [uint32 N_rx | uint32 seq_len | uint32 num_frames]
//         [float32 re, float32 im] × seq_len, channel-major, per frame.
static std::vector<Samples_2dim_t> load_dataset(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open dataset: " + path);

    uint32_t n_rx, seq_len, num_frames;
    f.read(reinterpret_cast<char*>(&n_rx),       sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&seq_len),    sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&num_frames), sizeof(uint32_t));

    std::vector<Samples_2dim_t> dataset(num_frames, Samples_2dim_t(n_rx, Samples_1dim_t(seq_len)));
    for (auto& frame : dataset)
        for (auto& ch : frame)
            for (auto& s : ch) {
                float re, im;
                f.read(reinterpret_cast<char*>(&re), sizeof(float));
                f.read(reinterpret_cast<char*>(&im), sizeof(float));
                s = Sample_t(re, im);
            }

    if (!f) throw std::runtime_error("Dataset file truncated: " + path);
    return dataset;
}

// main function
int main(int argc, char*argv[])
{
    std::signal(SIGINT, &sig_int_handler);

    // Run spectral MUSIC DoA algorithm
    std::string cmd = std::string(PYTHONPATH) + ' ' + std::string(MUSIC_PYFILE) + "&";
    system(cmd.c_str());

    // Load pre-generated GPS L1 C/A ULA dataset from MATLAB
    auto dataset = load_dataset(DATASET_FILE);
    if (dataset[0].size() != NUM_CHANNELS)
        throw std::runtime_error("Dataset channel count != NUM_CHANNELS");
    std::cout << "Loaded " << dataset.size() << " frames ("
              << dataset[0][0].size() << " samples/channel) from " << DATASET_FILE << std::endl;

    // ---------------------- Synchronization Worker ----------------------
    // GPS L1 C/A: compose the synchronizer configuration from the preset
    // helpers in sc_standards.h; the preamble is one full period of the
    // satellite-specific 1023-chip Gold code (sample rate 2.046 MHz at k=2)
    constexpr unsigned int prn             = 1;                     // satellite PRN number (must match dataset)
    constexpr unsigned int k               = 2;                     // samples per chip
    constexpr unsigned int m               = 3;                     // pulse filter delay [chips]
    constexpr float        beta            = 0.5f;                  // RRC roll-off (approximates rectangular chips)
    constexpr float        threshold       = 0.3f;                  // detection threshold
    constexpr float        dphi_max        = 0.015f;                // Doppler search range (+/-5 kHz at 2.046 MHz)
    constexpr unsigned int payload_sym_len = GPS_L1CA_CODE_LEN;     // capture one further code period
    static liquid_float_complex preamble[GPS_L1CA_CODE_LEN];        // C/A code chips (BPSK)
    scframesync_init_gps_l1ca(prn, preamble);

    SyncWorker<NUM_CHANNELS, scframesync_iface> sync(
        {{preamble, GPS_L1CA_CODE_LEN, LIQUID_FIRFILT_RRC, k, m, beta, threshold, dphi_max, payload_sym_len}},
        std::ref(stop_signal_called), 0);

    // ---------------------- Grouping Worker ----------------------
    GroupingWorker grouping_worker(NUM_CHANNELS, 1e6, std::ref(stop_signal_called));

    // ---------------------- Queue Connections ----------------------
    //Scheme:  |tx-worker|->queue->|rx-worker|
    // 1. get reference to rx-workers internal input queue
    // 2. add rx-workers input queue as tx-workers output queue

    // tx_queue_external->|zmq_tx_external_worker|
    ThreadSafeQueue<Samples_2dim_t> tx_queue_external;
    ZmqTxWorker zmq_tx_external_worker(IMPORT_INTERFACE, tx_queue_external, stop_signal_called);

    // |zmq_rx_worker|--|-->rx_queue[0]--|-->|sync_worker|
    //                  |-->   ...     --|
    //                  |-->rx_queue[i]--|
    auto& rx_queues = *sync.GetRxQueues();
    ZmqRxWorker zmq_rx_worker(IMPORT_INTERFACE, rx_queues, stop_signal_called);

    // |sync_worker|->frame_samps_queue->|grouping_worker|
    auto& frame_samps_queue = *grouping_worker.GetFrameSampsQueue();
    sync.AddFrameSampsQueue(std::ref(frame_samps_queue));

    // |sync_worker|->frame_syms_queue->|grouping_worker|
    auto& frame_syms_queue = *grouping_worker.GetFrameSymsQueue();
    sync.AddFrameSymsQueue(std::ref(frame_syms_queue));

    // |grouping_worker|->multi_ch_frame_samps_queue->|zmq_tx_worker|
    ThreadSafeQueue<Samples_2dim_t> tx_queue;
    ZmqTxWorker zmq_tx_worker(EXPORT_INTERFACE, tx_queue, stop_signal_called);
    grouping_worker.AddMultiChSampsQueue(std::ref(tx_queue));

    // |grouping_worker|->multi_ch_frame_syms_queue->|matlab_worker|
    MatlabXport m_xport(M_FILE);
    MatlabWorker matlab_worker(m_xport, stop_signal_called);
    grouping_worker.AddMultiChSymsQueue(std::ref(*matlab_worker.GetMultiChSymsQueue()));
    grouping_worker.AddMultiChSampsQueue(std::ref(*matlab_worker.GetMultiChSampsQueue()));
    matlab_worker.SetExportEnabled(false);

    // ---------------------- Terminal Worker ----------------------
    TerminalWorker terminal(stop_signal_called);
    terminal.SetPhaseCorrQueue(sync.GetPhaseCorrQueue());
    terminal.SetMatlabWorker(&matlab_worker);

    // ---------------------- Run Workers ----------------------
    sync.RunWorker();
    grouping_worker.RunWorker();
    matlab_worker.RunWorker();
    zmq_tx_external_worker.RunWorker();
    zmq_rx_worker.RunWorker();
    zmq_tx_worker.RunWorker();
    terminal.RunWorker();
    std::cout << "Started Workers..." << std::endl;

    // Cycle through dataset frames, pushing each to the synchronization pipeline
    size_t frame_idx = 0;
    while (!stop_signal_called.load()) {
        auto rx_copy = dataset[frame_idx];
        zmq_tx_external_worker.PushItemToQueue(tx_queue_external, std::move(rx_copy));
        frame_idx = (frame_idx + 1) % dataset.size();
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    };

    terminal.StopWorker();
    sync.StopWorker();
    grouping_worker.StopWorker();
    matlab_worker.StopWorker();
    zmq_tx_external_worker.StopWorker();
    zmq_rx_worker.StopWorker();
    zmq_tx_worker.StopWorker();
    std::cout << "Stopped Workers..." << std::endl;
    return 0;
}
