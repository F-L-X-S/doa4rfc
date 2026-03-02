/**
 * @file music_gnuradio.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2026-01-16
 * 
 */

 #include <iostream>
 #include <csignal>

 #include <liquid.h>
 #include <sync_worker.h>
 #include <zmq_if.h>

// RFC Settings
#define NUM_CHANNELS 1

//#define FLEXFRAMESYNC
#define OFDMFRAMESYNC

// ZMQ-socket for data import from Gnuradio
#define IMPORT_INTERFACE "tcp://*:5554" 

// ZMQ-socket for data export to MUSIC running in Python-application
#define EXPORT_INTERFACE "tcp://*:5555" 

// Python-Application with MUSIC algorithm for DoA estimation
#define PYTHONPATH "./music/env/bin/python"
#define MUSIC_PYFILE "./music/music-spectrum.py"               

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
    std::string cmd = std::string(PYTHONPATH) + ' ' + std::string(MUSIC_PYFILE)+"&";
    system(cmd.c_str());

    // ---------------------- Synchronization Worker ----------------------
    #ifdef OFDMFRAMESYNC
            //Define OFDM-Framesync parameters
            constexpr unsigned int M           = 256;   // number of subcarriers 
            constexpr unsigned int cp_len      = 20;    // cyclic prefix length 
            constexpr unsigned int taper_len   = 4;     // window taper length 
            static unsigned char p[M];                  // subcarrier allocation array
            ofdmframe_init_default_sctype(M, p);        // initialize subcarrier allocation
            SyncWorker<NUM_CHANNELS, ofdmframesync_iface> sync({M, cp_len, taper_len, p}, std::ref(stop_signal_called));
    #elif defined(FLEXFRAMESYNC)
            SyncWorker<NUM_CHANNELS, flexframesync_iface> sync({}, std::ref(stop_signal_called));
    #else 
        #error "Synchronizer-Type not supported: Define OFDMFRAMESYNC or FLEXFRAMESYNC"
    #endif
    
    // Add output-queues to sync-worker 
    // FrameSampsQueue_t frame_samps_queue;
    // sync.AddFrameSampsQueue(std::ref(frame_samps_queue));
    // FrameSymsQueue_t frame_syms_queue;
    // sync.AddFrameSymsQueue(std::ref(frame_syms_queue));

    // ---------------------- ZMQ Worker ----------------------
    // ZMQ-socket for data import from Gnuradio
    auto& rx_queues = *sync.GetRxQueues();
    ZmqRxWorker<NUM_CHANNELS> zmq_rx_worker(IMPORT_INTERFACE, rx_queues, stop_signal_called);

    // ZMQ socket for data export to MUSIC Python-application
    MultiChFrameSampsQueue_t tx_queue;
    ZmqTxWorker<MultiChFrameSamps_t> zmq_tx_worker(EXPORT_INTERFACE, tx_queue, stop_signal_called);

    // ---------------------- Run Workers ----------------------
    sync.RunWorker();   
    zmq_rx_worker.RunWorker();
    zmq_tx_worker.RunWorker();
    std::cout << "Started Workers..." << std::endl;
    // Let run for 10 sec
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));

    sync.StopWorker();
    zmq_rx_worker.StopWorker();
    zmq_tx_worker.StopWorker();
    std::cout << "Stopped Workers..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    sync.RunWorker();   
    zmq_rx_worker.RunWorker();
    zmq_tx_worker.RunWorker();
    std::cout << "Started Workers..." << std::endl;
    // Let run for 10 sec
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));

    sync.StopWorker();
    zmq_rx_worker.StopWorker();
    zmq_tx_worker.StopWorker();
    std::cout << "Stopped Workers..." << std::endl;
    return 0;
}