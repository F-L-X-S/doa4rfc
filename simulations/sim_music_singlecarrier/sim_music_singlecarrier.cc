/**
 * @file sim_music_singlecarrier.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief Simulation of single-carrier transmission for DoA estimation using MUSIC algorithm
 * A complex single-carrier baseband signal is generated and transmitted over a simulated channel with noise, frequency offset and time-delay.
 * The time-delay between the different channels is set according to the desired direction of arrival for an uniform linear antenna array (ULA) with lambda/2 spacing.
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
 #include <multisync/multisync.h>
 #include <zmq_socket/zmq_socket.h>

// Definition of the transmission-settings 
#define FRAME_PADDING 30            // Noisy samples around the frame (before and after) 
#define NUM_CHANNELS 4              // Number of simulated channels 
#define SAMPLE_RATE 3.84e6f         // Sample rate [Hz] 
#define CARRIER_FREQUENCY 6.0e5f    // Carrier Frequency [Hz]

#define PAYLOAD_LEN 480                 // Payload length (bytes)
#define MOD_SCHEME LIQUID_MODEM_QPSK    // Modulation scheme
#define CHECK LIQUID_CRC_16             // Data validity check
#define FEC0 LIQUID_FEC_NONE            // Inner forward error-correction
#define FEC1 LIQUID_FEC_NONE            // Outer forward error-correction

// Definition of the channel impairments
#define NOISE_FLOOR -90.0f          // Noise floor (dB) 
#define SNR_DB 40.0f                // Signal-to-noise ratio (dB) 
#define CARRIER_FREQ_OFFSET 0.0f    // Carrier frequency offset (radians per sample)
#define CARRIER_PHASE_OFFSET 0.0f   // Phase offset (radians) 
#define DELAY 1.0f                  // Time-delay [Samples]
#define DDELAY 1.6f                 // Differential Delay between receiving channels [Samples] 

// ZMQ-socket for data export to MUSIC running in Python-application
#define EXPORT_INTERFACE 'tcp://localhost:5555' 

// Python-Application with MUSIC algorithm for DoA estimation
#define PYTHONPATH "./music/env/bin/python"
#define MUSIC_PYFILE "./music/music-spectrum.py"               

// custom data type to pass to callback function
struct callback_data {
    int frame_detected = 0;                     // Flag indicating if a frame was detected
};

// callback function for frame synchronizer
static int callback(unsigned char *  _header,
                    int              _header_valid,
                    unsigned char *  _payload,
                    unsigned int     _payload_len,
                    int              _payload_valid,
                    framesyncstats_s _stats,
                    void *           _userdata)
{
    printf("******** callback invoked\n");

    // count bit errors (assuming all-zero message)
    unsigned int bit_errors = 0;
    unsigned int i;
    for (i=0; i<_payload_len; i++)
        bit_errors += liquid_count_ones(_payload[i]);

    framesyncstats_print(&_stats);
    printf("    header crc          :   %s\n", _header_valid ?  "pass" : "FAIL");
    printf("    payload length      :   %u\n", _payload_len);
    printf("    payload crc         :   %s\n", _payload_valid ?  "pass" : "FAIL");
    printf("    payload bit errors  :   %u / %u\n", bit_errors, 8*_payload_len);

    // Set detection Flag 
    static_cast<callback_data*>(_userdata)->frame_detected = 1;  

    return 0;
}

// Sample type
using Sample_t = std::complex<float>; 

// main function
int main(int argc, char*argv[])
{
    // Run spectral MUSIC DoA algorithm
    std::string cmd = std::string(PYTHONPATH) + ' ' + std::string(MUSIC_PYFILE)+"&";
    system(cmd.c_str());

    // ---------------------- Signal Generation ----------------------
    // create flexframegen object
    flexframegenprops_s fgprops;
    flexframegenprops_init_default(&fgprops);
    fgprops.mod_scheme  = MOD_SCHEME;
    fgprops.check       = CHECK;
    fgprops.fec0        = FEC0;
    fgprops.fec1        = FEC1;
    flexframegen fg = flexframegen_create(&fgprops);

    // assemble frame with default payload (NULL-ptr)
    flexframegen_assemble(fg, NULL, NULL, PAYLOAD_LEN);

    // Complex baseband signal buffer (transmitted sequence)
    unsigned int frame_len = flexframegen_getframelen(fg);
    std::vector<Sample_t> tx(frame_len);            

    // Write Samples to transmit buffer
    while (!flexframegen_write_samples(fg, tx.data(), tx.size())){
        tx.resize(tx.size()+1);
    };

    // destroy flexframegen object
    flexframegen_destroy(fg);

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
    unsigned int m          =   5;              // filter semi-length
    unsigned int npfb       =   1000;           // fractional delay resolution

    // Initialize buffer to hold the received baseband signals
    unsigned int sequnece_len = frame_len + FRAME_PADDING;
    std::vector<std::vector<Sample_t>> rx(NUM_CHANNELS, std::vector<Sample_t>(sequnece_len));           

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
        InsertSequence(rx[ch].data(), tx.data(), FRAME_PADDING/2, tx.size());

        // Processing
        for (unsigned int i = 0; i < sequnece_len; ++i) {
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
    MultiSync<flexframesync_iface> ms(NUM_CHANNELS, {}, callback, userdata);

    // Frame Samples 
    std::vector<std::vector<Sample_t>> frame_samps(NUM_CHANNELS);                               // samples of detected frames for all channels
    frame_samps.assign(NUM_CHANNELS, std::vector<Sample_t>(frame_len, Sample_t(0.0f, 0.0f)));   // Initialize the buffer with zeros

    // Samplewise synchronization of each channel (MultiSync processes whole buffer, in this case we want to limit the buffer to only one sample)
    std::vector<std::complex<float>> rx_sample(1);                              // Buffer to hold current sample for sample-by-sample processing
    for (unsigned int i = 0; i < sequnece_len; ++i) {
        for (unsigned int j = 0; j < NUM_CHANNELS; ++j){
            // execute the respective synchronizer
            rx_sample[0]= rx[j][i];             
            ms.Execute(j, &rx_sample);

            // Store raw samples of detected frames of all channels 
            if (cb_data[j].frame_detected == 1) {
                ms.GetFrameSamps(j, &frame_samps[j]); 
                cb_data[j].frame_detected = 0;  
            };
        };    
    };

    // ---------------------- Export Data ----------------------
    ZmqSender sender("tcp://*:5555");                    
    sender.send(frame_samps);   

    return 0;
}