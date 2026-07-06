/**
 * @file wifi_sim.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 *
 * @brief Reads a pre-generated IEEE 802.11n ULA dataset (binary file produced by
 * wifi_ula_dataset_gen.m) and feeds the per-element baseband samples into the
 * doa4rfc synchronization pipeline via ZMQ, cycling through all frames.
 *
 * @version 0.1
 * @date 2026-06-15
 *
 */

 #include <iostream>
 #include <fstream>
 #include <csignal>
 #include <stdexcept>

 #include <liquid.h>
 #include <doa4rfc.h>
 #include <wlan_standards.h>
 #include <sync_worker.h>
 #include <grouping_worker.h>
 #include <multithread_worker.h>
 #include <zmq_if.h>
 #include <matlab_if.h>
 #include <signal_generator.h>
 #include <ui_worker.h>

using namespace doa4rfc;

// Number of ULA channels — must match N_rx used in wifi_ula_dataset_gen.m
#define NUM_CHANNELS 4

// Binary dataset file written by wifi_ula_dataset_gen.m.
// Regenerate whenever N_rx, DOA, MCS or SNR settings change.
#define DATASET_FILE "simulations/wifi/records/wifi11n_20_MCS7_N4_DOA30deg_SNR20dB.bin"

// ZMQ-socket for import of the generated baseband samples
#define IMPORT_INTERFACE "tcp://127.0.0.1:5554"

// ZMQ-socket for data export to MUSIC running in Python-application
#define EXPORT_INTERFACE "tcp://127.0.0.1:5555"

// Python-Application with MUSIC algorithm for DoA estimation
#define PYTHONPATH "./music/env/bin/python"
#define MUSIC_PYFILE "./music/music-spectrum.py"

// MATLAB output file to store results
#define M_FILE "simulations/wifi/wifi_sim.m"

// Signal handler to stop by keyboard interrupt
std::atomic<bool> stop_signal_called(false);
void sig_int_handler(int) {
    stop_signal_called = true;
}

// Reads binary dataset written by wifi_ula_dataset_gen.m.
// Layout: [uint32 N_rx | uint32 frame_len | uint32 num_frames]
//         [float32 re, float32 im] × frame_len, channel-major, per frame.
static std::vector<Samples_2dim_t> load_dataset(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open dataset: " + path);

    uint32_t n_rx, frame_len, num_frames;
    f.read(reinterpret_cast<char*>(&n_rx),       sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&frame_len),  sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&num_frames), sizeof(uint32_t));

    std::vector<Samples_2dim_t> dataset(num_frames, Samples_2dim_t(n_rx, Samples_1dim_t(frame_len)));
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

    // Load pre-generated 802.11n ULA dataset from MATLAB
    auto dataset = load_dataset(DATASET_FILE);
    if (dataset[0].size() != NUM_CHANNELS)
        throw std::runtime_error("Dataset channel count != NUM_CHANNELS");
    std::cout << "Loaded " << dataset.size() << " frames ("
              << dataset[0][0].size() << " samples/channel) from " << DATASET_FILE << std::endl;

    // ---------------------- Synchronization Worker ----------------------
    // IEEE 802.11n HT-mixed, 20 MHz: compose the synchronizer configuration
    // from the preset helpers in wlanframesync.h
    constexpr unsigned int M          = 64;   // FFT size (20 MHz channelization)
    constexpr unsigned int cp_len     = 16;   // cyclic prefix length (long GI: 800ns = 16 samples)
    constexpr unsigned int stf_period = 16;   // L-STF time-domain periodicity (0.8us = 16 samples)
    constexpr unsigned int ltf_count  = 2;    // L-LTF long training symbol repetitions
    static unsigned char p[M];                // subcarrier allocation (52 data, 4 pilots)
    static liquid_float_complex stf_seq[M];   // L-STF training sequence (freq)
    static liquid_float_complex ltf_seq[M];   // L-LTF training sequence (freq)
    static float pilot_base[4];               // 20 MHz pilot base pattern
    wlanframesync_init_sctype_80211n_20(M, p);
    wlanframesync_init_lstf_80211(M, stf_seq);
    wlanframesync_init_lltf_80211(M, ltf_seq);
    wlanframesync_init_pilot_base_80211_20(pilot_base);

    SyncWorker<NUM_CHANNELS, wlanframesync_iface> sync(
        {{M, cp_len, p, stf_seq, stf_period, ltf_seq, ltf_count, pilot_base}},
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
