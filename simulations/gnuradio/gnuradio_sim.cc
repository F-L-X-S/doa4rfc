/**
 * @file gnuradio_sim.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 *
 * @brief This simulation demonstrates the use of the doa4rfc framework with samples streamed from GNU Radio.
 * Instead of generating the baseband signal internally (see music_sim.cc), the IMPORT_INTERFACE receives the
 * multi-channel IQ samples from a GNU Radio flowgraph using the zmq_if_sink block (gnuradio/doa4rfc_zmq_if_sink.py).
 * The accompanying flowgraph script simulations/gnuradio/ofdm_doa_tx.py generates OFDM frames for all antennas
 * using GNU Radio's own OFDM blocks (no liquid-DSP on the TX side), phase-shifted according to a DoA of 30 degrees
 * (half-wavelength ULA). The frames are detected with the configurable wlanframesync, set up to match the
 * GNU Radio frame structure (see the Synchronization Worker section below).
 *
 * Usage: start this application first (binds the PULL socket on IMPORT_INTERFACE), then run the flowgraph
 * with GNU Radio's Python interpreter (see simulations/gnuradio/ofdm_doa_tx.py).
 *
 * @version 0.1
 * @date 2026-07-10
 *
 */

 #include <iostream>
 #include <csignal>

 #include <liquid.h>
 #include <doa4rfc.h>
 #include <sync_worker.h>
 #include <grouping_worker.h>
 #include <multithread_worker.h>
 #include <zmq_if.h>
 #include <matlab_if.h>
 #include <ui_worker.h>

using namespace doa4rfc;

// Number of ULA channels — must match NUM_CHANNELS in ofdm_doa_tx.py
#define NUM_CHANNELS 4

// ZMQ-socket for import of the baseband samples streamed by GNU Radio (zmq_if_sink block)
#define IMPORT_INTERFACE "tcp://127.0.0.1:5554"

// ZMQ-socket for data export to MUSIC running in Python-application
#define EXPORT_INTERFACE "tcp://127.0.0.1:5555"

// Python-Application with MUSIC algorithm for DoA estimation
#define PYTHONPATH "./music/env/bin/python"
#define MUSIC_PYFILE "./music/music-spectrum.py"

// MATLAB output file to store results
#define M_FILE "simulations/gnuradio/gnuradio_sim.m"

// Signal handler to stop by keaboard interrupt
std::atomic<bool> stop_signal_called(false);
void sig_int_handler(int) {
    stop_signal_called=true;
}

// main function
int main(int argc, char*argv[])
{
    std::signal(SIGINT, &sig_int_handler);

    // Run spectral MUSIC DoA algorithm
    std::string cmd = "OS_ACTIVITY_MODE=disable " + std::string(PYTHONPATH) + ' ' + std::string(MUSIC_PYFILE)+"&";
    system(cmd.c_str());

    // ---------------------- Synchronization Worker ----------------------
    // WLAN-framesync configuration matching the GNU Radio OFDM frames generated
    // by ofdm_doa_tx.py: 802.11a-style carrier layout (48 data carriers, pilots
    // at +/-7/+/-21), preamble [STF | STF | LTF] with per-symbol cyclic prefix.
    constexpr unsigned int M          = 64;   // FFT size
    constexpr unsigned int cp_len     = 16;   // cyclic prefix length
    constexpr unsigned int stf_period = 16;   // STF time-domain periodicity (every 4th carrier active)
    constexpr unsigned int ltf_count  = 1;    // single LTF training symbol
    static unsigned char p[M];                // subcarrier allocation (48 data, 4 pilots)
    static liquid_float_complex stf_seq[M];   // STF training sequence (freq)
    static liquid_float_complex ltf_seq[M];   // LTF training sequence (freq)
    static float pilot_base[4] = {1.0f, 1.0f, 1.0f, -1.0f};  // pilot base pattern, ascending k

    // Subcarrier allocation: all tones k = +/-1..26 active, pilots at +/-7/+/-21
    for (unsigned int i = 0; i < M; ++i) p[i] = OFDMFRAME_SCTYPE_NULL;
    for (int k = -26; k <= 26; ++k) {
        if (k == 0) continue;
        p[(k + M) % M] = (k == -21 || k == -7 || k == 7 || k == 21) ?
            OFDMFRAME_SCTYPE_PILOT : OFDMFRAME_SCTYPE_DATA;
    }

    // Sync-word sign patterns — fixed (numpy RandomState(42)), hardcoded
    // identically in ofdm_doa_tx.py; keep both in sync
    static const int   k_stf[12] = {-24,-20,-16,-12, -8, -4,  4,  8, 12, 16, 20, 24};
    static const float b_stf[12] = {  1, -1,  1,  1,  1, -1,  1,  1,  1, -1,  1,  1};
    static const float b_ltf[52] = { 1,  1, -1,  1, -1, -1, -1,  1, -1,  1, -1, -1, -1, -1, -1, -1, -1, -1,  1,  1, -1, -1, -1,  1, -1,  1,
                                     1,  1,  1,  1, -1, -1, -1, -1, -1,  1, -1, -1,  1, -1,  1, -1,  1, -1, -1,  1,  1,  1,  1,  1,  1,  1};
    for (unsigned int i = 0; i < M; ++i) { stf_seq[i] = {0.0f, 0.0f}; ltf_seq[i] = {0.0f, 0.0f}; }
    for (unsigned int i = 0; i < 12; ++i)
        stf_seq[(k_stf[i] + M) % M] = {b_stf[i] * (float)M_SQRT2, 0.0f};
    for (int k = -26, i = 0; k <= 26; ++k) {
        if (k == 0) continue;
        ltf_seq[(k + M) % M] = {b_ltf[i++], 0.0f};
    }

    SyncWorker<NUM_CHANNELS, wlanframesync_iface> sync(
        {{M, cp_len, p, stf_seq, stf_period, ltf_seq, ltf_count, pilot_base}},
        std::ref(stop_signal_called), 0);

    // ---------------------- Grouping Worker ----------------------
    GroupingWorker grouping_worker(NUM_CHANNELS, 1e6, std::ref(stop_signal_called));   // max_age of 1ms for grouping

    // ---------------------- Queue Connections ----------------------
    //Scheme:  |tx-worker|->queue->|rx-worker|
    // 1. get reference to rx-workers internal input queue
    // 2. add rx-workers input queue as tx-workers output queue

    // |zmq_rx_worker|--|-->rx_queue[0]--|-->|sync_worker|
    //                  |-->   ...     --|
    //                  |-->rx_queue[i]--|
    auto& rx_queues = *sync.GetRxQueues();
    // ZMQ-socket for data import from GNU Radio (zmq_if_sink block)
    ZmqRxWorker zmq_rx_worker(IMPORT_INTERFACE, rx_queues, stop_signal_called);


    // |sync_worker|->frame_samps_queue->|grouping_worker|
    auto& frame_samps_queue = *grouping_worker.GetFrameSampsQueue();    // Get reference to internal input queue
    sync.AddFrameSampsQueue(std::ref(frame_samps_queue));               // Add rx workers input queue as tx workers output queue

    // |sync_worker|->frame_syms_queue->|grouping_worker|
    auto& frame_syms_queue = *grouping_worker.GetFrameSymsQueue();      // Get reference to internal input queue
    sync.AddFrameSymsQueue(std::ref(frame_syms_queue));                 // Add rx workers input queue as tx workers output queue

    // |grouping_worker|->multi_ch_frame_samps_queue->|zmq_tx_worker|
    // ZMQ-socket for data export to MUSIC Python-application
    ThreadSafeQueue<Samples_2dim_t> tx_queue;
    ZmqTxWorker zmq_tx_worker(EXPORT_INTERFACE, tx_queue, stop_signal_called);
    grouping_worker.AddMultiChSampsQueue(std::ref(tx_queue));           // Add tx workers input queue as grouping worker output queue

    // |grouping_worker|->multi_ch_frame_syms_queue->|zmq_tx_syms_worker|
    ThreadSafeQueue<Symbols_2dim_t> tx_syms_queue;
    ZmqTxWorker zmq_tx_syms_worker(EXPORT_INTERFACE, tx_syms_queue, stop_signal_called, ZmqMsgType::Symbols);
    grouping_worker.AddMultiChSymsQueue(std::ref(tx_syms_queue));       // Add syms tx workers input queue as grouping worker output queue

    // |grouping_worker|->multi_ch_frame_syms_queue->|matlab_worker|
    MatlabXport m_xport(M_FILE);                                        // MatlabXport instance to store results in a .m file
    MatlabWorker matlab_worker(m_xport, stop_signal_called);
    grouping_worker.AddMultiChSymsQueue(std::ref(*matlab_worker.GetMultiChSymsQueue()));   // Add matlab workers input queue as grouping worker output queue
    grouping_worker.AddMultiChSampsQueue(std::ref(*matlab_worker.GetMultiChSampsQueue())); // Add matlab workers input queue as grouping worker output queue
    matlab_worker.SetExportEnabled(false);                                                 // Deactivate continuous export to .m file by default (activate via terminal)

    // ---------------------- Terminal Worker ----------------------
    TerminalWorker terminal(stop_signal_called);
    terminal.SetPhaseCorrQueue(sync.GetPhaseCorrQueue());
    terminal.SetMatlabWorker(&matlab_worker);

    // ---------------------- Run Workers ----------------------
    sync.RunWorker();
    grouping_worker.RunWorker();
    matlab_worker.RunWorker();
    zmq_rx_worker.RunWorker();
    zmq_tx_worker.RunWorker();
    zmq_tx_syms_worker.RunWorker();
    terminal.RunWorker();
    std::cout << "Started Workers..." << std::endl;
    std::cout << "Waiting for samples from GNU Radio on " << IMPORT_INTERFACE
              << " (run simulations/gnuradio/ofdm_doa_tx.py)" << std::endl;

    // Wait until program is exited by user...
    while (!stop_signal_called.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    };

    terminal.StopWorker();
    sync.StopWorker();
    grouping_worker.StopWorker();
    matlab_worker.StopWorker();
    zmq_rx_worker.StopWorker();
    zmq_tx_worker.StopWorker();
    zmq_tx_syms_worker.StopWorker();
    std::cout << "Stopped Workers..." << std::endl;
    return 0;
}
