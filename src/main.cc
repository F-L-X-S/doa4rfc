/**
 * @file main.cc
 * @author Felix Schuelke 
 * @brief DoA Estimation of an OFDM / Singlecarrier signal using an uniform linear antenna array (lambda-half spacing) and USRP N210 SDRs.
 * 1. Generation of baseband samples of a multicarrier OFDM signal or single-carrier signal 
 * 2. Cyclic transmission of the generated frame 
 * 3. Reception of the transmitted signal at 2 receiving channels (RX antennas)
 * 4. Frame detection synchronization for each receiving channel
 * 5. Grouping of the detected frames baseband samples and data symbols across the receiving channels
 * 6. a) Export of the grouped baseband samples to a MUSIC Python-application via ZMQ-socket
 *    b) Export of the grouped data symbols to the specified .m file for visualization in MATLAB
 * 
 * Select the signal modulation by defining the preprocessor directive OFDMFRAME or FLEXFRAME. 
 * 
 * Ettus example project for USRP integartion: https://kb.ettus.com/Getting_Started_with_UHD_and_C%2B%2B
 * Liquid-DSP documentation: https://liquidsdr.org
 * Liquid-DSP project: https://github.com/jgaeddert/liquid-dsp
 * 
 * @version 0.1
 * @date 2025-07-01
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <uhd_if.h>
#include <uhd/utils/thread_priority.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>

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
#include <signal_generator.h>

using namespace doa4rfc;

// Definition of the transmission-settings 
#define NUM_CHANNELS 2              // Number of multipath channels (RX antennas)
#define SAMPLE_RATE 3.84e6          // Sample rate [Hz] 
#define CARRIER_FREQUENCY 1.25e9    // Carrier Frequency [Hz]

// Frame-generator parameters (OFDMFRAME/FLEXFRAME)
#define PAYLOAD_LEN 4                   // Payload length (bytes)
#define MOD_SCHEME LIQUID_MODEM_QPSK    // Modulation scheme
#define CHECK LIQUID_CRC_16             // Data validity check
#define FEC0 LIQUID_FEC_NONE            // Inner forward error-correction
#define FEC1 LIQUID_FEC_NONE            // Outer forward error-correction

// Select Modulation Type 
//#define FLEXFRAME
#define OFDMFRAME

// ZMQ-socket for import of the generated baseband samples
#define IMPORT_INTERFACE "tcp://127.0.0.1:5554" 

// ZMQ-socket for data export to MUSIC running in Python-application
#define EXPORT_INTERFACE "tcp://127.0.0.1:5555" 

// Python-Application with MUSIC algorithm for DoA estimation
#define PYTHONPATH "./music/env/bin/python"
#define MUSIC_PYFILE "./music/music-spectrum.py"             

// MATLAB output file to store results
#define M_FILE "measurements/music.m"

// Signal handler to stop by keaboard interrupt
std::atomic<bool> stop_signal_called(false);
void sig_int_handler(int) {
    stop_signal_called=true;
}

// Main function 
int UHD_SAFE_MAIN(int argc, char *argv[]) {
    uhd::set_thread_priority_safe();
    std::signal(SIGINT, &sig_int_handler);

    // Run spectral MUSIC DoA algorithm
    std::string cmd = "OS_ACTIVITY_MODE=disable " + std::string(PYTHONPATH) + ' ' + std::string(MUSIC_PYFILE)+"&";
    system(cmd.c_str());

    // ---------------------- Framegeneration ----------------------
    // Framegenerator parameters
    #ifdef OFDMFRAME
        ofdmflexframegenprops_s fgprops;
        ofdmflexframegenprops_init_default(&fgprops);
    #elif defined(FLEXFRAME)
        flexframegenprops_s fgprops;
        flexframegenprops_init_default(&fgprops);
    #endif

    // Set modulation scheme, data validity check and forward error-correction schemes
    fgprops.mod_scheme  = MOD_SCHEME;
    fgprops.check       = CHECK;
    fgprops.fec0        = FEC0;
    fgprops.fec1        = FEC1;

    // Assemble frame and write samples to transmit buffer
    #ifdef OFDMFRAME    
        //Define OFDM parameters
        constexpr unsigned int M           = 256;   // number of subcarriers 
        constexpr unsigned int cp_len      = 20;    // cyclic prefix length 
        constexpr unsigned int taper_len   = 4;     // window taper length 
        static unsigned char p[M];                  // subcarrier allocation array
        ofdmframe_init_default_sctype(M, p);        // initialize subcarrier allocation

       ofdmflexframegen fg = ofdmflexframegen_create(M,cp_len,taper_len,p,&fgprops);

        // initialize header/payload and assemble frame
        unsigned int i;
        unsigned char header[8];
        unsigned char payload[PAYLOAD_LEN];
        for (i=0; i<8; i++)
            header[i] = i & 0xff;
        for (i=0; i<PAYLOAD_LEN; i++)
            payload[i] = rand() & 0xff;
        ofdmflexframegen_assemble(fg, header, payload, PAYLOAD_LEN);

        // Complex baseband signal buffer (transmitted sequence)
        std::vector<Sample_t> tx;
        unsigned int symbol_len = M + cp_len;

        // Write one OFDM symbol per iteration until the frame is complete
        int frame_complete = 0;
        while (!frame_complete) {
            size_t offset = tx.size();
            tx.resize(offset + symbol_len);
            frame_complete = ofdmflexframegen_write(fg, liquid_conv::Ptr(tx.data() + offset), symbol_len);
        }
        unsigned int frame_len = tx.size();

        // destroy flexframegen object
        ofdmflexframegen_destroy(fg);

        
    #elif defined(FLEXFRAME)
        flexframegen fg = flexframegen_create(&fgprops);

        // assemble frame with default payload (NULL-ptr)
        flexframegen_assemble(fg, NULL, NULL, PAYLOAD_LEN);

        // Complex baseband signal buffer (transmitted sequence)
        unsigned int frame_len = flexframegen_getframelen(fg);
        std::vector<Sample_t> tx(frame_len);            

        // Write Samples to transmit buffer
        while (!flexframegen_write_samples(fg, liquid_conv::Ptr(tx.data()), tx.size())){
            tx.resize(tx.size()+1);
        };

        // destroy flexframegen object
        flexframegen_destroy(fg);
    #endif

   // ---------------------- UHD Interface Stream Worker ----------------------
    // USRP Constants
    unsigned long int DAC_RATE = 400e6;             // USRP DAC Rate (N210 fixed to 400MHz)
    unsigned long int ADC_RATE = 100e6;             // USRP ADC Rate (N210 fixed to 100MHz)

    // TX/RX Settings 
    double center_freq = CARRIER_FREQUENCY;         // Carrier frequency 
    double txrx_rate = SAMPLE_RATE;                 // Sample rate  
    unsigned int tx_cycle = 1500;                   // Transmit testframe every ... [ms]
    double max_age = 0.45*(double)tx_cycle/1000;    // max time delta between CFRs to group together [s]

    // TX 
    // NOTE : the sample rate computation MUST be in double precision so
    //        that the UHD can compute its interpolation rate properly
    unsigned int interp_rate = (unsigned int)(DAC_RATE / txrx_rate);
    interp_rate = (interp_rate >> 2) << 2;      // ensure multiple of 4
    double usrp_tx_rate = DAC_RATE / (double)interp_rate;

    // RX
    // NOTE : the sample rate computation MUST be in double precision so
    //        that the UHD can compute its decimation rate properly
    unsigned int decim_rate = (unsigned int)(ADC_RATE / txrx_rate);
    decim_rate = (decim_rate >> 1) << 1;        // ensure multiple of 2
    double usrp_rx_rate = ADC_RATE / (float)decim_rate;

    //create USRP devices
    std::array<uhd::usrp::multi_usrp::sptr, 2> usrps {
        uhd::usrp::multi_usrp::make("addr=192.168.10.3"), 
        uhd::usrp::multi_usrp::make("addr=192.168.168.2")
    };

    // Receive stream confguration
    uhd::stream_args_t stream_args("fc32");                                                   // convert internal sc16 to complex float 32
    stream_args.args["recv_buff_size"] = "100000000"; // 100MB Buffer
    uhd::rx_streamer::sptr rx_stream_0 = usrps[0]->get_rx_stream(stream_args);                // create receive streams 
    uhd::rx_streamer::sptr rx_stream_1 = usrps[1]->get_rx_stream(stream_args);                // cretae a receive stream 
    size_t max_samps = rx_stream_0->get_max_num_samps();  

    // Start streaming
    std::thread t0(stream_worker<NUM_CHANNELS>, std::ref(usrps), 
        std::ref(max_samps), std::ref(usrp_tx_rate), std::ref(usrp_rx_rate), std::ref(center_freq), 
        double(750) ,std::ref(stop_signal_called));
    std::this_thread::sleep_for(std::chrono::milliseconds(3*tx_cycle));

    // ---------------------- Synchronization Worker ----------------------
    #ifdef OFDMFRAME
            SyncWorker<NUM_CHANNELS, ofdmframesync_iface> sync({M, cp_len, taper_len, p}, std::ref(stop_signal_called));
    #elif defined(FLEXFRAME)
            SyncWorker<NUM_CHANNELS, flexframesync_iface> sync({}, std::ref(stop_signal_called));
    #else 
        #error "Synchronizer-Type not supported: Define OFDMFRAME or FLEXFRAME"
    #endif

    // ---------------------- Grouping Worker ----------------------
    GroupingWorker grouping_worker(NUM_CHANNELS, max_age, std::ref(stop_signal_called)); 

    // |sync_worker|->frame_samps_queue->|grouping_worker|
    sync.AddFrameSampsQueue(std::ref(*grouping_worker.GetFrameSampsQueue())); 

    // |sync_worker|->frame_syms_queue->|grouping_worker|
    sync.AddFrameSymsQueue(std::ref(*grouping_worker.GetFrameSymsQueue()));                 

    // ---------------------- UHD Interface Receive workers ----------------------
    // RX Resampling rate
    usrp_rx_rate = usrps[0]->get_rx_rate(0);
    double rx_resamp_rate = txrx_rate / usrp_rx_rate;
    std::cout << boost::format("Required RX Resampling Rate: %f ") % (rx_resamp_rate) << std::endl;

    // Thread-safe queues 
    std::array<SampleBlockQueue_t, 2>& rx_queues = *sync.GetRxQueues();

    std::thread t1(rx_worker<4096>, rx_stream_0, std::ref(rx_queues[0]), std::ref(stop_signal_called));
    std::thread t2(rx_worker<4096>, rx_stream_1, std::ref(rx_queues[1]), std::ref(stop_signal_called));

    // ---------------------- UHD Interface Transmit workers ----------------------
    // TX stream configuration 
    uhd::tx_streamer::sptr tx_stream_0 = usrps[0]->get_tx_stream(stream_args); 

    // TX Arbitrary Resampler 
    usrp_tx_rate = usrps[0]->get_tx_rate(0);
    double tx_resamp_rate = usrp_tx_rate / txrx_rate;
    std::cout << boost::format("Required TX Resampling Rate: %f ") % (tx_resamp_rate) << std::endl;

    // Transmission thread 
    std::thread t3(tx_worker, std::ref(tx_stream_0), std::ref(tx), tx_cycle, std::ref(stop_signal_called));

    // ---------------------- MatlabXport worker ----------------------
    // |grouping_worker|->multi_ch_frame_syms_queue->|matlab_worker|
    MatlabXport m_xport(M_FILE);                                                            // MatlabXport instance to store results in a .m file                                  
    MatlabWorker matlab_worker(m_xport, stop_signal_called);
    grouping_worker.AddMultiChSymsQueue(std::ref(*matlab_worker.GetMultiChSymsQueue()));    // Add matlab workers input queue as grouping worker output queue

    // ---------------------- Zmq worker ----------------------
    // ZMQ-socket for data export to MUSIC Python-application
    // |grouping_worker|->multi_ch_frame_samps_queue->|zmq_tx_worker|
    ThreadSafeQueue<Samples_2dim_t> tx_queue;
    ZmqTxWorker zmq_tx_worker(EXPORT_INTERFACE, tx_queue, stop_signal_called);
    grouping_worker.AddMultiChSampsQueue(std::ref(tx_queue));           // Add tx workers input queue as grouping worker output queue

    // |grouping_worker|->multi_ch_frame_syms_queue->|zmq_tx_syms_worker|
    ThreadSafeQueue<Symbols_2dim_t> tx_syms_queue;
    ZmqTxWorker zmq_tx_syms_worker(EXPORT_INTERFACE, tx_syms_queue, stop_signal_called, ZmqMsgType::Symbols);
    grouping_worker.AddMultiChSymsQueue(std::ref(tx_syms_queue));       // Add syms tx workers input queue as grouping worker output queue

    // ---------------------- Configure Terminal worker ----------------------
    TerminalWorker terminal(stop_signal_called);
    terminal.SetPhaseCorrQueue(sync.GetPhaseCorrQueue());
    terminal.SetMatlabWorker(&matlab_worker);

    // ---------------------- Continue in main thread ----------------------
    sync.RunWorker();
    grouping_worker.RunWorker();
    matlab_worker.RunWorker();
    zmq_tx_worker.RunWorker();
    zmq_tx_syms_worker.RunWorker();
    terminal.RunWorker();
    std::cout << "Started Receiving..." << std::endl;

    while (!stop_signal_called.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    terminal.StopWorker();
    sync.StopWorker();
    grouping_worker.StopWorker();
    matlab_worker.StopWorker();
    zmq_tx_worker.StopWorker();
    zmq_tx_syms_worker.StopWorker();

    t0.join();
    t1.join();
    t2.join();
    t3.join();

    std::cout << "Stopped receiving...\n" << std::endl;

return EXIT_SUCCESS;
}