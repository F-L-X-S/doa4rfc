/**
 * @file sim_multichannel.cc
 * @brief This example illustrates the synchronization of OFDM frames in a multi-channel environment.
 * 
 * 
 * Framegeneration and synchronization for ofdmframesync is demonstrated in Liquid-DSP documentation on 
 * https://github.com/jgaeddert/liquid-dsp (Copyright (c) 2007 - 2016 Joseph Gaeddert).
 * 
 * Signal Parameters:
 * SampleRate: 1 
 * Noise Floor: -90 dB (1e-9 W)
 * Signal-to-Noise Ratio (SNR): 40 dB (10e3) 
 * => Signal power |X|^2 = 40dB-90dB = -50 dB (1e-5W = 1e-9W * 10e3) 
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
 *          Subcarrier freq. spacing: df = SampleRate / M 
 *              -> For M=64 and SampleRate =1: df = 1/64 
 *          Subcarrier frequency: f_k = f_c + k * df
 *              -> For f_c = 0 (no carrier-modulation): f_k = k * df = k / M
 *          Time-delay: tau [seconds] = Tau [samples]*(1/SampleRate)
 *              -> For SampleRate = 1, tau [samples] = tau [seconds]
 *          Phase shift on subcarrier k due to time-delay tau [seconds]: dphi_k = 2 * pi * f_k * tau = 2*pi*(f_c + k*df)*tau
 *              -> For f_c=0, SampleRate = 1:  dphi_k = 2pi * (k/M) * Tau 
 * 
 * e.g. DELAY = 0.1 samples, DDELAY = 0.1 samples 
 *      -> total delay: CH0: 0.1 samples, CH1: 0.2 samples, CH2: 0.3 samples, CH3: 0.4 samples
 * 
 *      CH0 (tau=0.1)       k=-15:  dphi_-15 =  2*pi * -15/64 * -0.1     = 0.1473 rad
 *                          k=15:   dphi_15 =   2*pi * 15/64 * -0.1      = -0.1473 rad
 *      CH1 (tau=0.2)       k=-15:  dphi_-15 =  2*pi * -15/64 * -0.2     = 0.2945 rad
 *                          k=15:   dphi_15 =   2*pi * 15/64 * -0.2      = -0.2945 rad
 *      CH2 (tau=0.3)       k=-15:  dphi_-15 =  2*pi * -15/64 * -0.3     = 0.4418 rad
 *                          k=15:   dphi_15 =   2*pi * 15/64 * -0.3      = -0.4418 rad
 *      CH3 (tau=0.4)       k=-15:  dphi_-15 =  2*pi * -15/64 * -0.4     = 0.5890 rad
 *                          k=15:   dphi_15 =   2*pi * 15/64 * -0.4      = -0.5890 rad
 * 
 * Note, that the fdelay filter causes phase-distortion on some delay-values. 
 * The predefined values of DELAY and DDELAY are chosen to avoid this distortion.
 * 
 * @version 0.1
 * @date 2025-05-20
 * 
 * @copyright Copyright (c) 2025
 *  
 */

 #include <iostream>
 #include <cmath>
 #include <complex>
 #include <cassert>
 #include <liquid.h>
 #include <signal_generator/signal_generator.h>
 #include <matlab_export/matlab_export.h>
 #include <multisync.h>

// Definition of the transmission-settings 
#define NUM_SAMPLES 1200            // Total Number of samples to be generated 
#define SYMBOLS_PER_FRAME 3         // Number of data-symbols transmitted per frame
#define FRAME_START 30              // Start position of the ofdm-frame in the sequence
#define NUM_CHANNELS 4              // Number of simulated channels 
 

// Definition of the channel impairments
#define NOISE_FLOOR -90.0f          // Noise floor (dB) 
#define SNR_DB 40.0f                // Signal-to-noise ratio (dB) 
#define CARRIER_FREQ_OFFSET 0.0f    // Carrier frequency offset (radians per sample)
#define CARRIER_PHASE_OFFSET 0.0    // Phase offset (radians) 
#define DELAY 0.1f                  // Time-delay (samples)
#define DDELAY 0.1f                 // Differential Delay between receiving channels (samples)

// Output file in MATLAB-format to store results
#define OUTFILE "simulations/sim_multichannel/sim_multichannel.m" 

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

    std::vector<Sample_t> tx(frame_samples);          // Complex baseband signal buffer (transmitted sequence)
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

    // ------------------- Channel impairments ----------------------
    // Create reference channel and add impairments
    channel_cccf base_channel = channel_cccf_create();
    channel_cccf_add_carrier_offset(base_channel, CARRIER_FREQ_OFFSET, CARRIER_PHASE_OFFSET);    // Add Carrier Frequency Offset and Phase Offset

    // Define delay-filter parameters
    unsigned int nmax       = 200;  // maximum delay
    unsigned int m          =  12;  // filter semi-length
    unsigned int npfb       =  10;  // fractional delay resolution

    // Insert the baseband-sequence into the longer sequence at the specified start position 'TF_SYMBOL_START' 
    std::vector<Sample_t> tx_long(NUM_SAMPLES);                 // Complex baseband signal buffer (transmitted sequence in full length)  
    InsertSequence(tx_long.data(), tx.data(), FRAME_START, n);

    // Apply channel impairments and delay to the generated signal
    std::vector<std::vector<std::complex<float>>> rx(NUM_CHANNELS);                 // Complex baseband signal buffer for all channels (received sequence)
    for (unsigned int i = 0; i < NUM_CHANNELS; ++i) {
        std::vector<Sample_t> rx_ch(NUM_SAMPLES);                                   // Complex baseband signal buffer for single channel (received sequence)  

        // Add Time delay 
        fdelay_crcf fd = fdelay_crcf_create(nmax, m, npfb);
        fdelay_crcf_set_delay(fd, DELAY+i*DDELAY);                                  // Set the delay for respective channel
        fdelay_crcf_execute_block(fd, tx_long.data(), NUM_SAMPLES, rx_ch.data());       

        // Add channel impairments
        channel_cccf channel = channel_cccf_copy(base_channel);                     // Copy the base channel
        channel_cccf_add_awgn(channel, NOISE_FLOOR, SNR_DB);                        // Set unique noise for each channel
        channel_cccf_execute_block(channel, rx_ch.data(), NUM_SAMPLES, rx_ch.data());     
        
        // Copy sequence to the buffer for the respective channel
        rx[i] = rx_ch;     
        
        // Destroy channel and filter 
        channel_cccf_destroy(channel);
        fdelay_crcf_destroy(fd);
    }

    // Destroy reference channel 
    channel_cccf_destroy(base_channel);

    
    // ----------------- Synchronization ----------------------
    struct callback_data cb_data[NUM_CHANNELS];                 // Callback data buffer 
    void* userdata[NUM_CHANNELS];                               // Pointers to callback data buffer for each channel 

    // Array of Pointers to CB-Data 
    for (unsigned int i = 0; i < NUM_CHANNELS; ++i)
        userdata[i] = &cb_data[i];

    // Create multi-channel frame synchronizer
    MultiSync<ofdmframesync_iface> ms(NUM_CHANNELS, {M, cp_len, taper_len, p}, callback, userdata);

    // Samplewise synchronization of each channel (MultiSync processes whole buffer, in this case we want to limit the buffer to only one sample)
    std::vector<std::complex<float>> rx_sample(1);          // Buffer to hold current sample for sample-by-sample processing
    for (unsigned int i = 0; i < NUM_SAMPLES; ++i) {
        for (unsigned int j = 0; j < NUM_CHANNELS; ++j){
            rx_sample[0]= rx[j][i];                         // Get the current sample of the j-th channel
            ms.Execute(j, &rx_sample);                      // Execute the synchronizer for j-th channel 

            // Store the CFR of all channels (only once)
            if (cb_data[j].buffer.size() && !cb_data[j].cfr.size()){
                ms.GetFrameSamps(j, &cb_data[j].cfr); 
            };
        };
    };

    // ----------------- MATLAB Export ----------------------
    MatlabExport m_file(OUTFILE);                                   // Create MATLAB export instance
    
    // Export variables for all channels to MATLAB file
    for (unsigned int ch = 0; ch < NUM_CHANNELS; ++ch) {
        std::string ch_suffix = std::to_string(ch);

        m_file.Add(rx[ch], "x_" + ch_suffix)                        // Add received signal to MATLAB file  
            .Add(cb_data[ch].buffer, "datasymbols_" + ch_suffix)    // Add detected symbols to MATLAB file
            .Add(cb_data[ch].cfr, "cfr_" + ch_suffix);              // Add estimated CFR to MATLAB file
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