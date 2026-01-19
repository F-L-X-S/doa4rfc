/**
 * @file sim_music.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * 
 * Signal Parameters:
 *      Noise Floor: -90 dB (1e-9 W)
 *      Signal-to-Noise Ratio (SNR): 40 dB (10e3) 
 *      => Signal power |X|^2 = 40dB-90dB = -50 dB (1e-5W = 1e-9W * 10e3) 
 * 
 *      SAMPLE_RATE to set the time-base for the signal.
 *          -> One simulated baseband or modulated sample corresponds to a time step of 1/SAMPLE_RATE seconds
 *          ->n-th sample is at t = n/SAMPLE_RATE
 * 
 *      CARRIER_FREQUENCY:
 *          Modulation of each n-th sample x[n] with NCO (Numerically Controlled Oscillator):
 *              Analog Carrier Signal: exp(j*2*pi*CARRIER_FREQUENCY*t + phi)
 *              -> Upconversion tx[n] = tx[n]*exp(j*2*pi*CARRIER_FREQUENCY*n/SAMPLE_RATE)
 *              -> Downconversion rx[n] = rx[n]*exp(-j*2*pi*CARRIER_FREQUENCY*n/SAMPLE_RATE + j*CARRIER_PHASE_OFFSET)
 * 
 * OFDM Parameters: 
 * M (Number of subcarriers): 64
 * M_pilot (Number of pilot subcarriers): 6
 * M_data (Number of data subcarriers): 44
 * M_S0 (Number of enabled subcarriers in S0 (STF)): 24 (Note, that Liquid enables every second subcarrier in S0 -> differs e.g. from the IEEE 802.11 standard)
 * M_S1 (Number of enabled subcarriers in S1 (LTF)): 50
 * CP length: 16 
 * 
 * CFR gain on k-th subcarrier |H_k|: 
 *          |S|=1  (training symbols S on subcarrier k defined with amplitude 1)
 *          |X_k|^2 = (|X|^2)/(M_pilot+M_data) = 1e-5W / 50 = 2.0000e-07 W (Signal power on subcarrier k)
 *          |X_k|= sqrt(2.0000e-07 W) = 4.4721e-04 V (Signal amplitude on subcarrier k)
 * 
 *          CFR gain on subcarrier k (|H_k|) is defined as the ratio of the signal amplitude on subcarrier k X_k to the amplitude of the training symbol S_k,
 *          but FFT is normalized on subcarriers in Liquid-DSP -> multiply by M to get the expected CFR gain given by the synchronizer:
 *          -> |H_k| = [|X_k|/|S_k|]* M = (4.4721e-04 / 1)* 64 = 0.0286 
 * 
 * CFR phase on k-th subcarrier dphi_k: 
 *          Subcarrier freq. spacing: df = SAMPLE_RATE / M 
 *          Subcarrier frequency: f_k = CARRIER_FREQUENCY + k * df
 *          Time-delay: tau [seconds] = Tau [samples]*(1/SAMPLE_RATE)
 *          Phase shift on subcarrier k due to time-delay tau [seconds]: dphi_k = 2 * pi * f_k * tau 
 *              dphi_k = 2*pi*(CARRIER_FREQUENCY + k*SAMPLE_RATE/M)*Tau/SAMPLE_RATE
 * 
 * 
 * Time-delay Tau (DDELAY) between neighboring antennas in ULA with lambda/2 spacing (lambda for CARRIER_FREQUENCY): 
 *      -> tau [seconds]=sin(theta)/(2*CARRIER_FREQUENCY)
 *      -> Tau [samples] = tau [seconds] * SAMPLE_RATE = 0.5*sin(theta)*SAMPLE_RATE/CARRIER_FREQUENCY
 * 
 * e.g. theta=60°,  CARRIER_FREQUENCY = 6.0e5, SAMPLE_RATE = 3.84e6
 *      -> Tau [samples] = sin(60°)/(2*6.0e5) * 3.84e6  = 2.7713 samples
 * 
 * e.g. theta=45°,  CARRIER_FREQUENCY = 6.0e5, SAMPLE_RATE = 3.84e6
 *      -> tau [seconds] = sin(45°)/(2*6.0e5 Hz) = 5.8926e-05 seconds
 *      -> Tau [samples] = 5.8926e-05 seconds * 3.84e6 *  = 2.2627 samples
 * 
 * e.g. theta=30°,  CARRIER_FREQUENCY = 6.0e5, SAMPLE_RATE = 3.84e6
 *      -> Tau [samples] = sin(30°)/(2*6.0e5) * 3.84e6  = 1.6 samples
 * 
 * Note, that an increased basis-delay of approximately DELAY = 10 samples ensures a better performance of the fractional delay filter.
 * 
 * @version 0.1
 * @date 2025-08-19
 * 
 */

 #include <iostream>
 #include <cmath>
 #include <complex>
 #include <cassert>
 
 #include <liquid.h>
 #include <signal_generator/signal_generator.h>
 #include <matlabXport.hpp>
 #include <multisync.h>
 #include <zmq_if.h>

// Definition of the transmission-settings 
#define NUM_SAMPLES 1200            // Total Number of samples to be generated 
#define SYMBOLS_PER_FRAME 3         // Number of data-symbols transmitted per frame
#define FRAME_START 30              // Start position of the ofdm-frame in the sequence
#define NUM_CHANNELS 4              // Number of simulated channels 
#define SAMPLE_RATE 3.84e6f         // Sample rate [Hz] 
#define CARRIER_FREQUENCY 6.0e5f    // Carrier Frequency [Hz]

// Definition of the channel impairments
#define NOISE_FLOOR -90.0f          // Noise floor (dB) 
#define SNR_DB 40.0f                // Signal-to-noise ratio (dB) 
#define CARRIER_FREQ_OFFSET 0.0f    // Carrier frequency offset (radians per sample)
#define CARRIER_PHASE_OFFSET 0.0f   // Phase offset (radians) 
#define DELAY 10.0f                 // Time-delay [Samples]
#define DDELAY 1.6f              // Differential Delay between receiving channels [Samples] 

// Output file in MATLAB-format to store results
#define OUTFILE "simulations/sim_music/sim_music.m" 

// ZMQ-socket for data export to MUSIC running in Python-application
#define EXPORT_INTERFACE 'tcp://localhost:5555' 

// Python-Application with MUSIC algorithm for DoA estimation
#define PYTHONPATH "./music/env/bin/python"
#define MUSIC_PYFILE "./music/music-spectrum.py"               

// Sample type
using Sample_t = std::complex<float>; 

// custom data type to pass to callback function
struct callback_data {
    std::vector<Sample_t> buffer;        // Buffer to store detected symbols 
    std::vector<float> cfo;              // carrier frequency offsets estimated per sample 
    std::vector<Sample_t> cfr;           // channel frequency response 
};

// callback function
//  _X          : array of received subcarrier samples [size: _M x 1]
//  _p          : subcarrier allocation array [size: _M x 1]
//  _M          : number of subcarriers
//  _userdata   : user-defined data pointer
static int callback(Sample_t* _X, unsigned char * _p, unsigned int _M, void * _cb_data){
    // Add symbols from all subcarriers to buffer 
    for (unsigned int i = 0; i < _M; ++i) {
        // ignore 'null' and 'pilot' subcarriers
        if (_p[i] != OFDMFRAME_SCTYPE_DATA)
            continue;
        static_cast<callback_data*>(_cb_data)->buffer.push_back(_X[i]);  
    }
// No Reset after returning the first data symbol (return 0)
return 0;
}


// main function
int main(int argc, char*argv[])
{
    // Run spectral MUSIC DoA algorithm
    std::string cmd = std::string(PYTHONPATH) + ' ' + std::string(MUSIC_PYFILE)+"&";
    system(cmd.c_str());

    // ---------------------- Signal Generation ----------------------
    // options
    unsigned int M           = 64;      // number of subcarriers 
    unsigned int cp_len      = 16;      // cyclic prefix length (800ns for 20MHz => 16 Sample)
    unsigned int taper_len   = 4;       // window taper length 

    unsigned int frame_len   = M + cp_len;            // Frame-length in samples
    
    // Check if the number of samples is sufficient to contain the frame
    unsigned int frame_samples = (3+SYMBOLS_PER_FRAME)*frame_len; // S0a + S0b + S1 + data symbols
    assert(NUM_SAMPLES > frame_samples+FRAME_START); 

    // initialize subcarrier allocation
    unsigned char p[M];                               // subcarrier allocation array
    ofdmframe_init_default_sctype(M, p);

    // create frame generator
    ofdmframegen fg = ofdmframegen_create(M, cp_len, taper_len, p);

    std::vector<Sample_t> tx(frame_samples);     // Complex baseband signal buffer (transmitted sequence)
    unsigned int n=0;                                 // Number of generated time-domain baseband samples 

    // write first S0 symbol
    ofdmframegen_write_S0a(fg, &tx[n]);
    n += frame_len;

    // write second S0 symbol
    ofdmframegen_write_S0b(fg, &tx[n]);
    n += frame_len;

    // write S1 symbol
    ofdmframegen_write_S1( fg, &tx[n]);
    n += frame_len;

    // modulate data subcarriers
    std::vector<Sample_t> X(M);                 // channelized symbols
    for (size_t i=0; i<SYMBOLS_PER_FRAME; i++) {
        // load different subcarriers with different data
        unsigned int j;
        for (j=0; j<M; j++) {
            // ignore 'null' and 'pilot' subcarriers
            if (p[j] != OFDMFRAME_SCTYPE_DATA)
                continue;
            // Radnom QPSK Symbols
            X[j] = Sample_t((rand() % 2 ? -0.707f : 0.707f), (rand() % 2 ? -0.707f : 0.707f));
        }

        // Append OFDM symbol to time-domain complex baseband signal 
        ofdmframegen_writesymbol(fg, X.data(), &tx[n]);
        n += frame_len;
    }

    // Destroy frame generator
    ofdmframegen_destroy(fg);

    // ------------------- Upconversion ---------------------
    nco_crcf nco_tx = nco_crcf_create(LIQUID_NCO);
    nco_crcf_set_frequency(nco_tx, 2*M_PI*CARRIER_FREQUENCY/SAMPLE_RATE);
    nco_crcf_mix_block_up(nco_tx, tx.data(), tx.data(), tx.size());
    nco_crcf_destroy(nco_tx);                               

    // ------------------- Channel impairments and Downconversion ---------------------
    // Create reference channel
    channel_cccf base_channel = channel_cccf_create();

    // Delay filter parameters
    unsigned int nmax       =   200;            // maximum delay
    unsigned int m          =   cp_len;         // filter semi-length
    unsigned int npfb       =   1000;           // fractional delay resolution

    // Initialize buffer to hold the received baseband signals
    std::vector<std::vector<Sample_t>> rx(NUM_CHANNELS, std::vector<Sample_t>(NUM_SAMPLES));           

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
        InsertSequence(rx[ch].data(), tx.data(), FRAME_START, tx.size());

        // Processing
        for (unsigned int i = 0; i < NUM_SAMPLES; ++i) {
            fdelay_crcf_push(fd, rx[ch][i]);
            fdelay_crcf_execute(fd, &rx[ch][i]);                          // Apply Timedelay
            channel_cccf_execute(channel, rx[ch][i], &rx[ch][i]);         // Apply channel impairments 
            nco_crcf_mix_down(nco_rx, rx[ch][i], &rx[ch][i]);             // Apply Downconversion 
            nco_crcf_step(nco_rx);                                        // Step Carrier NCO
        }

        // Free Memory 
        fdelay_crcf_destroy(fd);        
        channel_cccf_destroy(channel);
        nco_crcf_destroy(nco_rx);  
    }

    // Destroy reference channel 
    channel_cccf_destroy(base_channel);

    // ----------------- Synchronization ----------------------
    struct callback_data cb_data[NUM_CHANNELS];                 // Callback data buffer 
    void* userdata[NUM_CHANNELS];                               // Pointers to callback data buffer for each channel 

    // Array of Pointers to CB-Data 
    for (unsigned int i = 0; i < NUM_CHANNELS; ++i)
        userdata[i] = &cb_data[i];

    // Create multi frame synchronizer
    MultiSync<ofdmframesync_iface> ms(NUM_CHANNELS, {M, cp_len, taper_len, p}, callback, userdata);

    // Channel frequency response (CFR) 
    std::vector<std::vector<Sample_t>> cfr(NUM_CHANNELS);                       // Multidimensional buffer to store the cfr for all channels
    cfr.assign(NUM_CHANNELS, std::vector<Sample_t>(M, Sample_t(0.0f, 0.0f)));   // Initialize the buffer with zeros

    // Samplewise synchronization of each channel (MultiSync processes whole buffer, in this case we want to limit the buffer to only one sample)
    std::vector<std::complex<float>> rx_sample(1);                              // Buffer to hold current sample for sample-by-sample processing
    for (unsigned int i = 0; i < NUM_SAMPLES; ++i) {
        for (unsigned int j = 0; j < NUM_CHANNELS; ++j){
            // execute the respective synchronizer
            rx_sample[0]= rx[j][i];             
            ms.Execute(j, &rx_sample);

            // Store the CFR of all channels (only once)
            if (cb_data[j].buffer.size() && !cb_data[j].cfr.size()){
                ms.GetFrameSamps(j, &cb_data[j].cfr);                                  // Write cfr to callback data
                cfr[j].assign(cb_data[j].cfr.begin(), cb_data[j].cfr.end());    // Copy the CFR to the multidimensional buffer   
            };
        };
    };

    // ZMQ socket for data export 
    ZmqSender sender("tcp://*:5555");
    sender.send(cfr);        


    // ----------------- MATLAB Export ----------------------
    MatlabXport m_file(OUTFILE);                                   // Create MATLAB export instance
    
    // Export variables for all channels to MATLAB file
    for (unsigned int ch = 0; ch < NUM_CHANNELS; ++ch) {
        std::string ch_suffix = std::to_string(ch);

        m_file.Add(rx[ch], "x_" + ch_suffix)                        // Add received signal to MATLAB file  
            .Add(cb_data[ch].buffer, "datasymbols_" + ch_suffix)    // Add detected symbols to MATLAB file
            .Add(cfr[ch], "cfr_" + ch_suffix);                      // Add estimated CFR to MATLAB file
    }

    // Initialize legend labels
    m_file.Add(
        "legend_labels = cell(1," + std::to_string(NUM_CHANNELS) + ");"
    );

    // Plot complex baseband signals 
    m_file.Add("figure;");
    for (unsigned int ch = 0; ch < NUM_CHANNELS; ++ch) {
            std::string ch_suffix = std::to_string(ch);
            m_file.Add(
                "subplot("+std::to_string(NUM_CHANNELS)+",1,"+std::to_string(ch+1)+");"
                "plot(real(x_"+ch_suffix+")); hold on;  plot(imag(x_"+ch_suffix+"));" 
                "title('Baseband Signal Channel "+std::to_string(ch)+"'), legend('Real', 'Imag');grid on;"
                "xlabel('Sample'); ylabel('Amplitude [V]');"
            );
    };

    // Add subcarrier index vector
    m_file.Add("M = length(cfr_0); subcarrier_idx = (-floor(M/2)):(M-floor(M/2)-1);");

    // Plot CFR Gains
    m_file.Add("figure; subplot(2,1,1); hold on; grid on;");
    for (unsigned int ch = 0; ch < NUM_CHANNELS; ++ch) {
        std::string ch_suffix = std::to_string(ch);
        m_file.Add(
            "plot(subcarrier_idx, abs(cfr_" + ch_suffix + "));"
            "legend_labels{" + std::to_string(ch + 1) + "} = sprintf('CH %d', " + std::to_string(ch) + ");"
        );
    }
    m_file.Add(
        "title('Channel Frequency Response Gain');"
        "xlabel('Subcarrier index'); ylabel('Gain [V^2]');"
        "xlim([subcarrier_idx(1), subcarrier_idx(end)]);"
        "legend(legend_labels, 'Location', 'best');"
        "hold off;"
    );

    // Plot CFR Phases
    m_file.Add("subplot(2,1,2); hold on; grid on;");
    for (unsigned int ch = 0; ch < NUM_CHANNELS; ++ch) {
        std::string ch_suffix = std::to_string(ch);
        m_file.Add(
            "plot(subcarrier_idx, angle(cfr_" + ch_suffix + "));"
        );
    }
    m_file.Add(
        "title('Channel Frequency Response Phase');"
        "xlabel('Subcarrier index'); ylabel('Phase [rad]');"
        "xlim([subcarrier_idx(1), subcarrier_idx(end)]);"
        "legend(legend_labels, 'Location', 'best');"
        "hold off;"
    );

    // Plot CFRs in complex plane
    m_file.Add("figure; hold on; grid on; axis equal;");
    for (unsigned int ch = 0; ch < NUM_CHANNELS; ++ch) {
        std::string ch_suffix = std::to_string(ch);
        m_file.Add(
            "plot(real(cfr_" + ch_suffix + "), imag(cfr_" + ch_suffix + "), '.', 'MarkerSize', 10);"
        );
    }
    m_file.Add(
        "xlabel('Real'); ylabel('Imaginary');"
        "title('Channel Frequency Response');"
        "axis([-1 1 -1 1]);"
        "legend(legend_labels, 'Location', 'best');"
        "hold off;"
    );

    return 0;
}