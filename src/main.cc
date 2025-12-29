/**
 * @file main.cc
 * @author Felix Schuelke 
 * @brief DoA Estimation of an OFDM transmitter using an uniform linear antenna array (lambda-half spacing)
 * 
 * 1. A complex OFDM Signal is generated and transmitted by the tx-worker
 * 2. The received samples of each channel are processed by separated rx-worker, which forward the received samples to the sync-worker
 * 3. The queued samples of each rx-channel are synchronized by a independent ofdm-synchronizer within the sync-worker. The synchronization is 
 *    possible for other modulation types by manipulating the MultiSync-class. 
 *      -> Detected data-symbols are forwarded as queued callback-data to the cbdata_export_worker, which exports them to the specified MATLAB-file
 *      -> The Channel Frequency Responses (CFRs) of detected frames are forwarded to the cfr_export_worker
 * 4. The cfr_export_worker writes the CFRs to the specified MATLAB file and forwards them via a ZMQ-socket to the music-spectrum.py
 *      -> The spatial MUSIC-spectrum is calculated by music-spectrum.py
 * 
 * The configuration of the USRP-hardware is performed by the stream-worker. 
 * All workers are defined in multi_rx.h 
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

#include <uhd/utils/thread_priority.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

#include <sdr_interfaces/sdr_interface_uhd.h>
#include <sync_worker/sync_worker.h>
#include <ui_worker/ui_worker.h>
#include <export_worker/export_worker.h>

#define NUM_CHANNELS 2                                          // Number of Channels (USRP-devices)   
#define SYMBOLS_PER_FRAME 1                                     // Number of Symbols to send per frame 
#define OUTFILE_CFR "./measurements/music_125MHz_M256_1m/music_125MHz_M256_1m_10deg/cfr.m"         // Output file in MATLAB-format to store results
#define OUTFILE_CBDATA "./measurements/music_125MHz_M256_1m/music_125MHz_M256_1m_10deg/cbdata.m"   // Output file in MATLAB-format to store results

#define PYTHONPATH "./music/env/bin/python"
#define MUSIC_PYFILE "./music/music-spectrum.py"                // Python script with MUSIC algorithm for DoA estimation
#define EXPORT_INTERFACE 'tcp://localhost:5555'                 // Interface for zmq socket

// Use OFDM-frame synchronizer for multi-channel synchronization
using Sync_t = MultiSync<ofdmframesync>;

// Signal handler to stop by keaboard interrupt
std::atomic<bool> stop_signal_called(false);
void sig_int_handler(int) {
    stop_signal_called=true;
}

// callback function
//  _X          : array of received subcarrier samples [size: _M x 1]
//  _p          : subcarrier allocation array [size: _M x 1]
//  _M          : number of subcarriers
//  _userdata   : user-defined data pointer
static int callback(std::complex<float>* _X, unsigned char * _p, unsigned int _M, void * _cb_data){
    // Add symbols from all subcarriers to buffer 
    for (unsigned int i = 0; i < _M; ++i) {
        // ignore 'null' and 'pilot' subcarriers
        if (_p[i] != OFDMFRAME_SCTYPE_DATA)
            continue;
        static_cast<CallbackData_t*>(_cb_data)->buffer.push_back(_X[i]);  
    }
    // Reset synchronizer after returning the first data symbol (return 1)
    return 1;
}

// Main function 
int UHD_SAFE_MAIN(int argc, char *argv[]) {
    uhd::set_thread_priority_safe();
    std::signal(SIGINT, &sig_int_handler);

    // ZMQ socket for data export 
    ZmqSender sender("tcp://*:5555");

    // Run spectral MUSIC DoA algorithm
    std::string cmd = std::string(PYTHONPATH) + ' ' + std::string(MUSIC_PYFILE)+"&";
    system(cmd.c_str());

    // Matlab Export destination file
    MatlabExport m_file_cfr(OUTFILE_CFR);
    MatlabExport m_file_cbdata(OUTFILE_CBDATA);

    // USRP Constants
    unsigned long int DAC_RATE = 400e6;             // USRP DAC Rate (N210 fixed to 400MHz)
    unsigned long int ADC_RATE = 100e6;             // USRP ADC Rate (N210 fixed to 100MHz)

    // TX/RX Settings 
    double center_freq = 1.25e9;                    // Carrier frequency 
    double txrx_rate = 3.84e6;                      // Sample rate  
    unsigned int tx_cycle = 1500;                    // Transmit every ... [ms]
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

    // ---------------------- Signal Generation in complex baseband ----------------------
    unsigned int M           = 256;     // number of subcarriers 
    unsigned int cp_len      = 20;      // cyclic prefix length 
    unsigned int taper_len   = 4;       // window taper length 
    unsigned char p[M];                 // subcarrier allocation array

    unsigned int frame_len   = M + cp_len;
    unsigned int frame_samples = (3+SYMBOLS_PER_FRAME)*frame_len; // S0a + S0b + S1 + data symbols

    // initialize subcarrier allocation
    ofdmframe_init_default_sctype(M, p);

    // create frame generator
    ofdmframegen fg = ofdmframegen_create(M, cp_len, taper_len, p);

    std::vector<std::complex<float>> tx_base(frame_samples);     // complex baseband signal buffer
    std::vector<std::complex<float>> X(M);                       // channelized symbols
    unsigned int n=0;                                            // Sample number in time domains

    // write first S0 symbol
    ofdmframegen_write_S0a(fg, &tx_base[n]);
    n += frame_len;

    // write second S0 symbol
    ofdmframegen_write_S0b(fg, &tx_base[n]);
    n += frame_len;

    // write S1 symbol
    ofdmframegen_write_S1( fg, &tx_base[n]);
    n += frame_len;

    // modulate data subcarriers
    for (size_t i=0; i<SYMBOLS_PER_FRAME; i++) {
        // load different subcarriers with different data
        unsigned int j;
        for (j=0; j<M; j++) {
            // ignore 'null' and 'pilot' subcarriers
            if (p[j] != OFDMFRAME_SCTYPE_DATA)
                continue;
            // Radnom BPSK Symbols
            X[j] = std::complex<float>((rand() % 2 ? -1.0f : 1.0f), 0.0f);
        }

        // generate OFDM symbol in the time domain
        ofdmframegen_writesymbol(fg, X.data(), &tx_base[n]);
        n += frame_len;
    }

   // ---------------------- Configure USRPs ----------------------
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

    // ---------------------- Configure Receive workers ----------------------
    // TX stream configuration 
    uhd::tx_streamer::sptr tx_stream_0 = usrps[0]->get_tx_stream(stream_args); 

    // RX Resampling rate
    usrp_rx_rate = usrps[0]->get_rx_rate(0);
    double rx_resamp_rate = txrx_rate / usrp_rx_rate;
    std::cout << boost::format("Required RX Resampling Rate: %f ") % (rx_resamp_rate) << std::endl;

    // callback data
    std::array<CallbackData_t, NUM_CHANNELS> cb_data;
    void* userdata[NUM_CHANNELS];

    // Array of Pointers to CB-Data 
    for (unsigned int i = 0; i < NUM_CHANNELS; ++i)
        userdata[i] = &cb_data[i];
        
    // Create multi frame synchronizer
    Sync_t ms(NUM_CHANNELS, {M, cp_len, taper_len, p}, callback, userdata);

    // Thread-safe queues 
    std::array<RxSamplesQueue_t, 2> rx_queues;

    std::thread t1(rx_worker<4096>, rx_stream_0, std::ref(rx_queues[0]), std::ref(stop_signal_called));
    std::thread t2(rx_worker<4096>, rx_stream_1, std::ref(rx_queues[1]), std::ref(stop_signal_called));

    // ---------------------- Configure Export workers ----------------------
    // Thread-safe queues 
    CfrQueue_t cfr_queue;
    CbDataQueue_t cbdata_queue;
    PhaseQueue_t phi_error_queue;

    std::thread t3(sync_worker<NUM_CHANNELS, Sync_t, CallbackData_t>, std::ref(ms), 
        std::ref(cb_data), std::ref(rx_queues), 
        std::ref(cfr_queue), std::ref(cbdata_queue),
        std::ref(phi_error_queue), std::ref(stop_signal_called));
    
    std::thread t4(cfr_export_worker<NUM_CHANNELS>, std::ref(cfr_queue), 
        max_age, std::ref(sender), std::ref(m_file_cfr), std::ref(stop_signal_called));
    
    std::thread t5(cbdata_export_worker, std::ref(cbdata_queue), std::ref(m_file_cbdata), std::ref(stop_signal_called));

    // ---------------------- Configure Transmit workers ----------------------

    // TX Arbitrary Resampler 
    usrp_tx_rate = usrps[0]->get_tx_rate(0);
    double tx_resamp_rate = usrp_tx_rate / txrx_rate;
    std::cout << boost::format("Required TX Resampling Rate: %f ") % (tx_resamp_rate) << std::endl;

    // Transmission thread 
    std::thread t6(tx_worker, std::ref(tx_stream_0), std::ref(tx_base), tx_cycle, std::ref(stop_signal_called));

    // ---------------------- Configure Terminal workers ----------------------
    std::thread t7(terminal_worker, std::ref(phi_error_queue), std::ref(stop_signal_called));

    // ---------------------- Continue in main thread ----------------------
    while (!stop_signal_called.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    rx_queues[0].cv.notify_all();
    rx_queues[1].cv.notify_all();
    cfr_queue.cv.notify_all();
    cbdata_queue.cv.notify_all();

    t0.join();
    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    t6.join();
    t7.join();

    std::cout << "Stopped receiving...\n" << std::endl;

return EXIT_SUCCESS;
}