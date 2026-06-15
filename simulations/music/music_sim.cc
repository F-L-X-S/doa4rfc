/**
 * @file music_sim.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief This simulation demonstrates the use of the doa4rfc framework for DoA  estimation using the MUSIC algorithm. 
 * It simulates a multipath-transmission of a baseband signal (multicarrier OFDM signal or single-carrier signal) with configurable impairments for each multipath channel (noise, time-delay, frequency and phase offset).
 * The isolated baseband samples of the detected frame are transmitted to the MUSIC Python-application via ZMQ-sockets.
 * 
 * Select the baseband signal by defining the preprocessor directive OFDMFRAME or FLEXFRAME. The estimated DoA spectrum is stored in a .m file for visualization in MATLAB.
 * 
 * @version 0.1
 * @date 2026-01-16
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
 #include <signal_generator.h>
 #include <ui_worker.h>

using namespace doa4rfc;

// Definition of the transmission-settings 
#define FRAME_PADDING 300           // Noisy samples around the frame (before and after) 
#define NUM_CHANNELS 4              // Number of simulated multipath channels 
#define SAMPLE_RATE 20e6f           // Sample rate [Hz] 
#define CARRIER_FREQUENCY 2.4121e9f // Carrier Frequency [Hz]

// Definition of the channel impairments
#define NOISE_FLOOR -90.0f          // Noise floor (dB) 
#define SNR_DB 40.0f                // Signal-to-noise ratio (dB) 
#define CARRIER_FREQ_OFFSET 0.0f    // Carrier frequency offset (radians per sample)
#define CARRIER_PHASE_OFFSET 0.0f   // Phase offset (radians) 
#define DELAY 1.0e-3f               // Time-delay [Samples]
#define DDELAY 2.07e-3f             // Differential Delay between receiving channels [Samples] 

// Frame-generator parameters (OFDMFRAME/FLEXFRAME)
#define PAYLOAD_LEN 4                   // Payload length (bytes)
#define MOD_SCHEME LIQUID_MODEM_QPSK    // Modulation scheme
#define CHECK LIQUID_CRC_16             // Data validity check
#define FEC0 LIQUID_FEC_NONE            // Inner forward error-correction
#define FEC1 LIQUID_FEC_NONE            // Outer forward error-correction

// Select Modulation Type 
// #define FLEXFRAME
#define OFDMFRAME

// ZMQ-socket for import of the generated baseband samples
#define IMPORT_INTERFACE "tcp://127.0.0.1:5554" 

// ZMQ-socket for data export to MUSIC running in Python-application
#define EXPORT_INTERFACE "tcp://127.0.0.1:5555" 

// Python-Application with MUSIC algorithm for DoA estimation
#define PYTHONPATH "./music/env/bin/python"
#define MUSIC_PYFILE "./music/music-spectrum.py"             

// MATLAB output file to store results
#define M_FILE "simulations/music/music_sim.m"

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
            constexpr unsigned int M           = 64;    // number of subcarriers 
            constexpr unsigned int cp_len      = 16;    // cyclic prefix length 
            constexpr unsigned int taper_len   = 4;     // window taper length 
            static unsigned char p[M];                  // subcarrier allocation array
            ofdmframe_init_default_sctype(M, p);        // initialize subcarrier allocation
            SyncWorker<NUM_CHANNELS, ofdmflexframesync_iface> sync({M, cp_len, taper_len, p}, std::ref(stop_signal_called), 0);
    #elif defined(FLEXFRAME)
            SyncWorker<NUM_CHANNELS, flexframesync_iface> sync({}, std::ref(stop_signal_called), 0);
    #else 
        #error "Synchronizer-Type not supported: Define OFDMFRAME or FLEXFRAME"
    #endif

    // ---------------------- Grouping Worker ----------------------
    GroupingWorker grouping_worker(NUM_CHANNELS, 1e6, std::ref(stop_signal_called));   // max_age of 1ms for grouping

    // ---------------------- Queue Connections ----------------------
    //Scheme:  |tx-worker|->queue->|rx-worker|
    // 1. get reference to rx-workers internal input queue
    // 2. add rx-workers input queue as tx-workers output queue

    // tx_queue_external->|zmq_tx_external_worker|
    ThreadSafeQueue<Samples_2dim_t> tx_queue_external;
    // ZMQ Worker simulating an external application providing samples (e.g. Gnuradio) 
    ZmqTxWorker zmq_tx_external_worker(IMPORT_INTERFACE, tx_queue_external, stop_signal_called);    

    // |zmq_rx_worker|--|-->rx_queue[0]--|-->|sync_worker|
    //                  |-->   ...     --|
    //                  |-->rx_queue[i]--|
    auto& rx_queues = *sync.GetRxQueues();
    // ZMQ-socket for data import from external application (e.g. from Gnuradio)
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

    // |grouping_worker|->multi_ch_frame_syms_queue->|matlab_worker|
    MatlabXport m_xport(M_FILE);                                        // MatlabXport instance to store results in a .m file                                  
    MatlabWorker matlab_worker(m_xport, stop_signal_called);
    grouping_worker.AddMultiChSymsQueue(std::ref(*matlab_worker.GetMultiChSymsQueue()));   // Add matlab workers input queue as grouping worker output queue
    grouping_worker.AddMultiChSampsQueue(std::ref(*matlab_worker.GetMultiChSampsQueue())); // Add matlab workers input queue as grouping worker output queue
    matlab_worker.SetExportEnabled(false);                                                 // Deactivate continuous export to .m file by default (activate via terminal)

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

    
    // initialize header/payload and assemble frame
        unsigned int i;
        unsigned char header[8];
        unsigned char payload[PAYLOAD_LEN];
        for (i=0; i<8; i++)
            header[i] = i & 0xff;
        for (i=0; i<PAYLOAD_LEN; i++)
            payload[i] = rand() & 0xff;

    // Assemble frame and write samples to transmit buffer
    #ifdef OFDMFRAME    
        ofdmflexframegen fg = ofdmflexframegen_create(M,cp_len,taper_len,p,&fgprops);
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
        flexframegen_assemble(fg, header, payload, PAYLOAD_LEN);

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

    // ------------------- Interpolation and Carriermodulation ---------------------
    // Upsampling factor: ensures DDELAY scales to >= 1 upsampled sample
    unsigned int upsample_factor = static_cast<unsigned int>(std::ceil(1.0f / DDELAY));
    unsigned int frame_len_up    = tx.size()    * upsample_factor;
    unsigned int sequnece_len    = tx.size()    + FRAME_PADDING;
    unsigned int sequence_len_up = sequnece_len * upsample_factor;

    std::vector<Sample_t> tx_up(frame_len_up, Sample_t(0.0f, 0.0f));

    // Interpolation 
    if (upsample_factor > 1) {
        firinterp_cccf interp_filt = firinterp_cccf_create_kaiser(upsample_factor, upsample_factor, 60.0f);
        firinterp_cccf_execute_block(interp_filt, liquid_conv::Ptr(tx.data()), tx.size(), liquid_conv::Ptr(tx_up.data()));
        firinterp_cccf_destroy(interp_filt);
    } else {
        tx_up = tx;
        upsample_factor = 1;
    }

    // Upconversion to carrier 
    nco_crcf nco_tx = nco_crcf_create(LIQUID_NCO);
    nco_crcf_set_frequency(nco_tx, 2.0f * M_PI * CARRIER_FREQUENCY / (SAMPLE_RATE * (float)upsample_factor));
    nco_crcf_mix_block_up(nco_tx, liquid_conv::Ptr(tx_up.data()), liquid_conv::Ptr(tx_up.data()), tx_up.size());
    nco_crcf_destroy(nco_tx);

    // Add frame-sequence to matlab-export
    m_xport.Add(tx_up, "TX_FRAME");
    m_xport.Add("figure;subplot(2,1,1); hold on;plot(real(TX_FRAME), 'DisplayName', 'Re(TX_FRAME)');plot(imag(TX_FRAME), 'DisplayName', 'Im(TX_FRAME)');");
    m_xport.Add("subplot(2,1,2); hold on;plot(abs(TX_FRAME), 'DisplayName', 'TX_FRAME')");
    m_xport.Add("subplot(2,1,1); title('Samples'); xlabel('Sample Index'); ylabel('Amplitude'); legend; grid on;");
    m_xport.Add("subplot(2,1,2); title('Samples Magnitude'); xlabel('Sample Index'); ylabel('Magnitude'); legend; grid on;");
    m_xport.Add("TX_FRAME_FFT = fftshift(fft(TX_FRAME));");
    m_xport.Add("TX_FRAME_FREQ = (-length(TX_FRAME_FFT)/2 : length(TX_FRAME_FFT)/2 - 1) / length(TX_FRAME_FFT);");
    m_xport.Add("figure;subplot(2,1,1); hold on;plot(TX_FRAME_FREQ, real(TX_FRAME_FFT), 'DisplayName', 'Re(FFT)');plot(TX_FRAME_FREQ, imag(TX_FRAME_FFT), 'DisplayName', 'Im(FFT)');");
    m_xport.Add("subplot(2,1,1); title('FFT TX\\_FRAME'); xlabel('Normalized Frequency'); ylabel('Amplitude'); legend; grid on;");
    m_xport.Add("subplot(2,1,2); hold on;plot(TX_FRAME_FREQ, abs(TX_FRAME_FFT), 'DisplayName', '|FFT|');");
    m_xport.Add("subplot(2,1,2); title('FFT TX\\_FRAME Magnitude'); xlabel('Normalized Frequency'); ylabel('Magnitude'); legend; grid on;");


    // ------------------- Channel impairments and Downconversion ---------------------
    // Create reference channel
    channel_cccf base_channel = channel_cccf_create();

    // Delay filter parameters
    unsigned int nmax       =   200;            // maximum delay
    unsigned int m          =   5;              // filter semi-length
    unsigned int npfb       =   1000;           // fractional delay resolution

    // Output buffer at baseband rate
    Samples_2dim_t rx(NUM_CHANNELS, Samples_1dim_t(sequnece_len));

    // Apply channel to the generated signal
    for (unsigned int ch = 0; ch < NUM_CHANNELS; ++ch) {
        // Configure time delay (scaled to upsampled rate)
        fdelay_crcf fd = fdelay_crcf_create(nmax, m, npfb);
        float delay = (DELAY + (float)(ch * DDELAY)) * (float)upsample_factor;
        fdelay_crcf_set_delay(fd, delay);

        // Configure channel impairments
        channel_cccf channel = channel_cccf_copy(base_channel);             // Copy the base channel
        channel_cccf_add_awgn(channel, NOISE_FLOOR, SNR_DB);                // Set unique noise for each channel

        // Configure Downconversion to complex baseband
        nco_crcf nco_rx = nco_crcf_create(LIQUID_NCO);
        nco_crcf_set_frequency(nco_rx, 2.0f * M_PI * CARRIER_FREQUENCY / (SAMPLE_RATE * (float)upsample_factor) + CARRIER_FREQ_OFFSET);
        nco_crcf_set_phase(nco_rx, CARRIER_PHASE_OFFSET);

        // Temporary upsampled buffer; insert upsampled tx at the padded offset
        std::vector<Sample_t> buf_up(sequence_len_up, Sample_t(0.0f, 0.0f));
        InsertSequence(buf_up.data(), tx_up.data(), (FRAME_PADDING / 2) * upsample_factor, frame_len_up);

        // Processing at upsampled rate
        for (unsigned int i = 0; i < sequence_len_up; ++i) {
            liquid_float_complex s_liquid = liquid_conv::Val(buf_up[i]);
            liquid_float_complex s_delayed;

            // Apply Timedelay
            fdelay_crcf_push(fd, s_liquid);
            fdelay_crcf_execute(fd, &s_delayed);

            // Apply channel impairments (on the delayed sample)
            liquid_float_complex s_out;
            channel_cccf_execute(channel, s_delayed, &s_out);

            // Apply Downconversion
            nco_crcf_mix_down(nco_rx, s_out, &s_out);
            nco_crcf_step(nco_rx);

            buf_up[i] = Sample_t(s_out.real, s_out.imag);
        }

        // Downsample back to baseband rate
        for (unsigned int i = 0; i < sequnece_len; ++i)
            rx[ch][i] = buf_up[i * upsample_factor];

        // Free Memory
        fdelay_crcf_destroy(fd);
        channel_cccf_destroy(channel);
        nco_crcf_destroy(nco_rx);
    }

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
    
    // Wait until program is exited by user... 
    while (!stop_signal_called.load()) {
        // Add generated Frame to queue for transmission to doa4rfc 
        auto rx_copy = rx;
        zmq_tx_external_worker.PushItemToQueue(tx_queue_external, std::move(rx_copy));

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