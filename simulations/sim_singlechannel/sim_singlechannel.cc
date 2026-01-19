/**
 * @file sim_singlechannel.cc
 * @brief This example illustrates the impact of a time-delay on the channel frequency response (CFR) of a synchronized OFDM frame, 
 * observed as a linear phase shift across the subcarriers.
 * 
 * It enables the simulation of frame synchronization under varying conditions of carrier frequency offset (CFO), 
 * carrier phase offset, and noise levels. The phase shift is applied in the complex baseband domain. 
 * 
 * For analysis and visualization in MATLAB, the channel frequency response, carrier frequency offset, and the data symbols 
 * estimated by the Liquid-DSP ofdmframesync synchronizer are exported.
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
 * e.g. DELAY = 0.5 samples:
 *      k=-15:  dphi_-15 =  2*pi * -15/64 * -0.5     = 0.7363 rad
 *      k=15:   dphi_15 =   2*pi * 15/64 * -0.5      = -0.7363 rad
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
 #include <matlabXport.hpp>

// Definition of the transmission-settings 
#define NUM_SAMPLES 1200            // Total Number of samples to be generated 
#define SYMBOLS_PER_FRAME 3         // Number of data-symbols transmitted per frame
#define FRAME_START 30              // Start position of the ofdm-frame in the sequence
 

// Definition of the channel impairments
#define NOISE_FLOOR -90.0f          // Noise floor (dB) 
#define SNR_DB 40.0f                // Signal-to-noise ratio (dB) 
#define CARRIER_FREQ_OFFSET 0.0f    // Carrier frequency offset (radians per sample)
#define CARRIER_PHASE_OFFSET 0.0    // Phase offset (radians) 
#define DELAY 0.5f                  // Time-delay (samples)

// Output file in MATLAB-format to store results
#define OUTFILE "./simulations/sim_singlechannel/sim_singlechannel.m" 

// Sample type
using Sample_t = std::complex<float>;   

// custom data type to pass to callback function
struct callback_data {
    std::vector<Sample_t> buffer;        // Buffer to store detected symbols 
    std::vector<float> cfo;                         // carrier frequency offsets estimated per sample 
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

    unsigned int frame_len   = M + cp_len;            // frame length in samples
    
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
    // create channel and add impairments
    channel_cccf channel = channel_cccf_create();
    channel_cccf_add_awgn(channel, NOISE_FLOOR, SNR_DB);                                    // Add Noise 
    channel_cccf_add_carrier_offset(channel, CARRIER_FREQ_OFFSET, CARRIER_PHASE_OFFSET);    // Add Carrier Frequency Offset and Phase Offset

    // create filter and set time-delay
    unsigned int nmax       = 200;  // maximum delay
    unsigned int m          =  12;  // filter semi-length
    unsigned int npfb       =  10;  // fractional delay resolution
    fdelay_crcf fd = fdelay_crcf_create(nmax, m, npfb);
    fdelay_crcf_set_delay(fd, DELAY);

    // Insert the baseband-sequence into the longer sequence at the specified start position 'TF_SYMBOL_START' 
    std::vector<Sample_t> tx_long(NUM_SAMPLES);                 // Complex baseband signal buffer (transmitted sequence in full length)  
    InsertSequence(tx_long.data(), tx.data(), FRAME_START, n);

    // Delay the transmitted sequence and apply channel impairments
    std::vector<Sample_t> rx(NUM_SAMPLES);                      // Complex baseband signal buffer (received sequence)  
    fdelay_crcf_execute_block(fd, tx_long.data(), NUM_SAMPLES, rx.data());                 
    channel_cccf_execute_block(channel, rx.data(), NUM_SAMPLES, rx.data());  

    // Destroy channel and filter
    channel_cccf_destroy(channel);
    fdelay_crcf_destroy(fd);

    // ----------------- Synchronization ----------------------
    struct callback_data cb_data; // callback data buffer

    // create frame synchronizer instance
    ofdmframesync fs = ofdmframesync_create(M, cp_len, taper_len, p, callback, (void*)&cb_data);

    // Synchronize the received sequence
    for (unsigned int i = 0; i < NUM_SAMPLES; ++i) {
        ofdmframesync_execute(fs,&rx[i], 1);                // execute synchronizer for each sample
        cb_data.cfo.push_back(ofdmframesync_get_cfo(fs));   // get the estimated CFO for each synchronized sample

        // Store the CFR after the first data symbol is detected
        if (cb_data.buffer.size() && !cb_data.cfr.size()){
            cb_data.cfr.resize(M);
            ofdmframesync_get_cfr(fs, &cb_data.cfr[0], M); 
        };
    };
    
    // Destroy synchronizer 
    ofdmframesync_destroy(fs);

    // ----------------- MATLAB Export ----------------------
    MatlabXport m_file(OUTFILE);           // Create MATLAB export instance
    m_file.Add(rx, "x")                     // Add received signal to MATLAB file          
    .Add(cb_data.buffer, "datasymbols")     // Add detected symbols to MATLAB file
    .Add(cb_data.cfo, "cfo")                // Add estimated CFO to MATLAB file
    .Add(cb_data.cfr, "cfr")                // Add estimated CFR to MATLAB file

    // Plot complex baseband signal
    .Add("figure; subplot(2,1,1); plot(real(x)); hold on;  plot(imag(x));" 
        "title('Baseband Signal'), legend('Real', 'Imag');grid on;"
        "xlabel('Sample'); ylabel('Amplitude [V]');")

    // Plot CFO estimate
    .Add("subplot(2,1,2); plot(cfo); title('Carrier frequency offset');grid on;"
        "xlabel('Sample'); ylabel('CFO [rad/sample]');")

    // Add subcarrier index vector
    .Add("M = length(cfr); subcarrier_idx = (-floor(M/2)):(M-floor(M/2)-1);")

    // Plot CFR Gain
    .Add("figure; subplot(2,1,1); plot(subcarrier_idx, abs(cfr)); title('Channel frequency response Gain');grid on;"
        "xlim([subcarrier_idx(1), subcarrier_idx(end)]); xlabel('Subcarrier index'); ylabel('Gain [V^2]');")

    // Plot CFR Phase
    .Add("subplot(2,1,2); plot(subcarrier_idx, angle(cfr)); title('Channel frequency response Phase');grid on;"
        "xlim([subcarrier_idx(1), subcarrier_idx(end)]); xlabel('Subcarrier index'); ylabel('Phase [rad]');")

    // Plot CFR in complex plane
    .Add("figure; plot(real(cfr), imag(cfr), '.', 'MarkerSize', 10);" 
        "grid on; axis equal; xlabel('Real'); ylabel('Imaginary');" 
        "title('Channel frequency response'); axis([-1 1 -1 1]);")

    // Plot detected symbols in complex plane
    .Add("figure; plot(real(datasymbols), imag(datasymbols), '.', 'MarkerSize', 10);" 
        "grid on; axis equal; xlabel('In-Phase'); ylabel('Quadrature');" 
        "title('Detected Symbols'); axis([-1 1 -1 1]);");

    return 0;
}