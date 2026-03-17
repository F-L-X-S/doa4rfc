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
 #include <doa4rfc.h>
 #include <sync_worker.h>
 #include <multithread_worker.h>
 #include <zmq_if.h>
 #include <signal_generator.h>

using namespace doa4rfc;

// Definition of the transmission-settings 
#define FRAME_PADDING 30            // Noisy samples around the frame (before and after) 
#define NUM_CHANNELS 4              // Number of simulated multipath channels 
#define SAMPLE_RATE 3.84e6f         // Sample rate [Hz] 
#define CARRIER_FREQUENCY 6.0e5f    // Carrier Frequency [Hz]

// Definition of the channel impairments
#define NOISE_FLOOR -90.0f          // Noise floor (dB) 
#define SNR_DB 40.0f                // Signal-to-noise ratio (dB) 
#define CARRIER_FREQ_OFFSET 0.0f    // Carrier frequency offset (radians per sample)
#define CARRIER_PHASE_OFFSET 0.0f   // Phase offset (radians) 
#define DELAY 1.0f                  // Time-delay [Samples]
#define DDELAY 1.6f                 // Differential Delay between receiving channels [Samples] 

// Frame-generator parameters (OFDMFRAME/FLEXFRAME)
#define PAYLOAD_LEN 480                 // Payload length (bytes)
#define MOD_SCHEME LIQUID_MODEM_QPSK    // Modulation scheme
#define CHECK LIQUID_CRC_16             // Data validity check
#define FEC0 LIQUID_FEC_NONE            // Inner forward error-correction
#define FEC1 LIQUID_FEC_NONE            // Outer forward error-correction

// Select Modulation Type 
#define FLEXFRAME
//#define OFDMFRAME

// ZMQ-socket for import of the generated baseband samples
#define IMPORT_INTERFACE "tcp://127.0.0.1:5552" 

// ZMQ-socket for data export to MUSIC running in Python-application
#define EXPORT_INTERFACE "tcp://127.0.0.1:5553" 

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
    #ifdef OFDMFRAME
            //Define OFDM-Framesync parameters
            constexpr unsigned int M           = 256;   // number of subcarriers 
            constexpr unsigned int cp_len      = 20;    // cyclic prefix length 
            constexpr unsigned int taper_len   = 4;     // window taper length 
            static unsigned char p[M];                  // subcarrier allocation array
            ofdmframe_init_default_sctype(M, p);        // initialize subcarrier allocation
            SyncWorker<NUM_CHANNELS, ofdmframesync_iface> sync({M, cp_len, taper_len, p}, std::ref(stop_signal_called));
    #elif defined(FLEXFRAME)
            SyncWorker<NUM_CHANNELS, flexframesync_iface> sync({}, std::ref(stop_signal_called));
    #else 
        #error "Synchronizer-Type not supported: Define OFDMFRAME or FLEXFRAME"
    #endif
    
    // Add output-queues to sync-worker 
    sync_worker_queues::FrameSampsQueue_t frame_samps_queue;
    sync.AddFrameSampsQueue(std::ref(frame_samps_queue));
    sync_worker_queues::FrameSymsQueue_t frame_syms_queue;
    sync.AddFrameSymsQueue(std::ref(frame_syms_queue));

    // ---------------------- ZMQ Worker ----------------------
    // ZMQ-socket for data import (e.g. from Gnuradio)
    auto& rx_queues = *sync.GetRxQueues();
    ZmqRxWorker<NUM_CHANNELS> zmq_rx_worker(IMPORT_INTERFACE, rx_queues, stop_signal_called);

    // ZMQ-socket for simulated data export of simulated baseband samples 
    // (simulated for testing purposes without gnuradio)
    ThreadSafeQueue<Samples_2dim_t> tx_queue_gr;
    ZmqTxWorker<Samples_2dim_t> zmq_tx_gr_worker(IMPORT_INTERFACE, tx_queue_gr, stop_signal_called);

    // ZMQ-socket for data export to MUSIC Python-application
    ThreadSafeQueue<Samples_2dim_t> tx_queue;
    ZmqTxWorker<Samples_2dim_t> zmq_tx_worker(EXPORT_INTERFACE, tx_queue, stop_signal_called);

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
        // Create frame generator
        ofdmflexframegen fg = ofdmflexframegen_create(M,cp_len,taper_len,p,&fgprops);

        // assemble frame with default payload (NULL-ptr)
        ofdmflexframegen_assemble(fg, NULL, NULL, PAYLOAD_LEN);

        // Complex baseband signal buffer (transmitted sequence)
        unsigned int frame_len = ofdmflexframegen_getframelen(fg);
        std::vector<Sample_t> tx(frame_len);            

        // Write Samples to transmit buffer
        while (!ofdmflexframegen_write(fg, liquid_conv::Ptr(tx.data()), tx.size())){
            tx.resize(tx.size()+(M + cp_len));
        };

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

    // ------------------- Upconversion ---------------------
    nco_crcf nco_tx = nco_crcf_create(LIQUID_NCO);
    nco_crcf_set_frequency(nco_tx, 2*M_PI*CARRIER_FREQUENCY/SAMPLE_RATE);
    nco_crcf_mix_block_up(nco_tx, liquid_conv::Ptr(tx.data()), liquid_conv::Ptr(tx.data()), tx.size());
    nco_crcf_destroy(nco_tx);                               

    // ------------------- Channel impairments and Downconversion ---------------------
    // Create reference channel
    channel_cccf base_channel = channel_cccf_create();

    // Delay filter parameters
    unsigned int nmax       =   200;            // maximum delay
    unsigned int m          =   5;              // filter semi-length
    unsigned int npfb       =   1000;           // fractional delay resolution

    // Initialize buffer to hold the received baseband signals
    unsigned int sequnece_len = frame_len + FRAME_PADDING;
    Samples_2dim_t rx(NUM_CHANNELS, Samples_1dim_t(sequnece_len));           

    // Apply channel to the generated signal
    for (unsigned int ch = 0; ch < NUM_CHANNELS; ++ch) {
        // Configure time delay 
        fdelay_crcf fd = fdelay_crcf_create(nmax, m, npfb);
        float delay = DELAY+(float)(ch*DDELAY);
        fdelay_crcf_set_delay(fd, delay);                                          

        // Configure channel impairments
        channel_cccf channel = channel_cccf_copy(base_channel);             // Copy the base channel
        channel_cccf_add_awgn(channel, NOISE_FLOOR, SNR_DB);                // Set unique noise for each channel

        // Configure Downconversion to complex baseband
        nco_crcf nco_rx = nco_crcf_create(LIQUID_NCO);
        nco_crcf_set_frequency(nco_rx, CARRIER_FREQ_OFFSET + 2*M_PI*(CARRIER_FREQUENCY/SAMPLE_RATE));
        nco_crcf_set_phase(nco_rx, CARRIER_PHASE_OFFSET);

        // Insert the baseband-sequence into the longer sequence at the specified start position 'TF_SYMBOL_START'
        InsertSequence(rx[ch].data(), tx.data(), FRAME_PADDING/2, frame_len);

        // Processing
        for (unsigned int i = 0; i < sequnece_len; ++i) {
            liquid_float_complex s_liquid = liquid_conv::Val(rx[ch][i]);
            liquid_float_complex s_delayed;                              // separate output buffer

            // Apply Timedelay
            fdelay_crcf_push(fd, s_liquid);
            fdelay_crcf_execute(fd, &s_delayed);                        // write to dedicated buffer

            // Apply channel impairments (on the delayed sample)
            liquid_float_complex s_out;
            channel_cccf_execute(channel, s_delayed, &s_out);

            // Apply Downconversion
            nco_crcf_mix_down(nco_rx, s_out, &s_out);
            nco_crcf_step(nco_rx);

            // Store result back to rx buffer
            rx[ch][i] = Sample_t(s_out.real, s_out.imag);
        }

        // Free Memory 
        fdelay_crcf_destroy(fd);        
        channel_cccf_destroy(channel);
        nco_crcf_destroy(nco_rx);  
    }

    // Destroy reference channel 
    channel_cccf_destroy(base_channel);


    // ---------------------- Run Workers ----------------------
    sync.RunWorker();   
    zmq_tx_gr_worker.RunWorker();
    zmq_rx_worker.RunWorker();
    zmq_tx_worker.RunWorker();
    std::cout << "Started Workers..." << std::endl;

    // Wait 3 sec...
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    // Add generated Frame to queue for transmission to doa4rfc 
    zmq_tx_gr_worker.PushItemToQueue<Samples_2dim_t>(tx_queue_gr, std::move(rx));
    
    // Wait 5 sec...
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    sync.StopWorker();
    zmq_tx_gr_worker.StopWorker();
    zmq_rx_worker.StopWorker();
    zmq_tx_worker.StopWorker();
    std::cout << "Stopped Workers..." << std::endl;
    return 0;
}