/**
 * @file multisync.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief This file contains the definition of the MultiSync class, which is a abstract class to handle multiple 
 * instances of generic frame synchronizers. Furthermore, it implements an NCO (Numerically Controlled Oscillator)
 * to compensate externally estimated phase- or frequency offsets before pushing samples into the synchronizer.
 * 
 * The MultiSync class utilizes the abstract SyncTraits struct that defines the Liquid-DSP interface for 
 * the specified synchronizer type. SyncTraits defines the C-function call of the Liquid-DSP synchronizer 
 * corresponding to the C++ functions used in MultiSync (Create, Reset, Execute, Destroy, GetFrameSamps).
 * 
 * Enabling MultiSync for different synchronizer types is achieved by specializing the SyncTraits struct for the required synchronizer type. 
 * 
 * The class provides C++-interfaces for different types of synchronizers, such as OFDM frame synchronizers.
 * The Liquid-DSP-interface of the specified Synchronizer is defined in the SyncTraits struct.
 * 
 * 
 * Liquid's DSP-modules are based on https://github.com/jgaeddert/liquid-dsp (Copyright (c) 2007 - 2016 Joseph Gaeddert).
 * 
 * @version 0.1
 * @date 2025-05-20
 * 
 * @copyright Copyright (c) 2025
 */

#ifndef MULTISYNC_H
#define MULTISYNC_H

#include <complex>
#include <cmath>
#include <cassert>
#include <concepts>
#include <iostream>
#include <limits>
#include <vector>
#include <liquid.h>
#include <synctraits.h>

#define DATA_WRITTEN 1
#define INDEX_OUT_OF_BOUND 0

/**
 * @brief Abstract MultiSync class to handle multiple instances of generic frame synchronizers. 
 * Enables simultaneous processing of oncoming samples by multiple synchronizers.
 * Implements an NCO (Numerically Controlled Oscillator) to compensate externally estimated phase- 
 * or frequency offsets before pushing samples into the synchronizer instances.
 * 
 * @tparam synchronizer_type Liquid-DSP frame synchronizer type 
 */
template<SyncTraitsConcept synchronizer_interface, std::size_t num_channels>          
class MultiSync {
public:

    /**
     * @brief Parameters to create an instance of the chosen synchronizer type
     *
     */
    using CreateParams_t   = typename synchronizer_interface::CreateParams_t;


    /**
     * @brief Construct a new MultiSync object with the specified number of channels and given synchronizer parameters.
     * Synchronizers will call the user-defined callback function with the user-defined data structure when receiving a frame.
     *
     * @param synchronizer_params synchronizer parameters
     * @param handler generic callback handler invoked on frame detection
     * @param userdata user-defined data passed to handler
     */
    MultiSync(
            const CreateParams_t&    synchronizer_params,
            GenericCallback_t        handler,
            void *                   userdata):
            cb_wrapper_{handler, userdata}
            {
                // create synchronizer instances for all channels
                framesync_ = new synchronizer_interface::SynchronizerType[num_channels];
                // create NCO instances for all channels
                nco_ = new nco_crcf[num_channels];
                // initialize NCO and synchronizer instances
                for (unsigned int i=0; i<num_channels; i++) {
                    framesync_[i] = synchronizer_interface::Create(synchronizer_params, &cb_wrapper_);
                    nco_[i] = nco_crcf_create(LIQUID_VCO);
                }
                // initialize per-channel recording buffers
                accum_buf_.resize(num_channels);
                frame_buf_.resize(num_channels);
            }

    /**
     * @brief Destroy the MultiSync object
     * 
     */
    ~MultiSync()
            {
                // Call Liquid-DSP destroy functions
                for (unsigned int i = 0; i < num_channels; i++){
                    synchronizer_interface::Destroy(framesync_[i]);
                    nco_crcf_destroy(nco_[i]);
                }
                // Free allocated memory
                delete[] framesync_;
                delete[] nco_;
            }

    /**
     * @brief Reset the MultiSync object and all associated synchronizers
     * 
     */
    void Reset()
            {
                for (unsigned int i = 0; i < num_channels; ++i) 
                    synchronizer_interface::Reset(framesync_[i]);
            };

    /**
     * @brief Process all channels simultaneously with a single sample.
     *
     * channel_samples[ch] holds the NCO-corrected input samples for channel ch.
     *
     * Channels are processed sequentially (ch 0 … ch N-1) on the same sample so
     * that each channel's synchronizer callback fires and the Execute() function returns
     * to enable processing the callback-data.
     *
     * Recording lifecycle:
     *
     *   record_index == 0  →  searching mode: every NCO-corrected sample is
     *       appended to the per-channel accumulation buffer accum_buf_.
     *
     *   record_index > 0, first time seen  →  snapshot trigger:
     *       All channels' accum_buf_ are trimmed to their last record_index samples
     *       and copied into frame_buf_. accum_buf_ entries are cleared and recording_
     *       is set to true. Subsequent blocks are appended directly to frame_buf_.
     *
     *   record_index > 0, recording_ already true  →  continue appending to frame_buf_.
     *
     *   record_index == 0 while recording_ == true  →  stop recording. frame_buf_
     *       contains the complete time-aligned multi-channel snapshot, ready to be
     *       read via GetMultiChannelFrameSamps(). accum_buf_ is left empty and
     *       refills naturally from the next call.
     *
     * @param channel_samples  One sample per channel
     * @param record_index     0 = searching; >0 = recording window size.
     */
    void Execute(std::array<Sample_t, num_channels>& channel_samples,
                        unsigned int record_index = 0)
                {
                    // Process each channel's sample
                    for (unsigned int ch = 0; ch < num_channels; ++ch) {
                        Sample_t* s = &channel_samples[ch];

                        // Apply phase offset to the sample
                        nco_crcf_mix_up(nco_[ch],
                            reinterpret_cast<liquid_float_complex*>(s),
                            reinterpret_cast<liquid_float_complex*>(s));

                        // Run the synchronizer on the block (may trigger callback)
                        synchronizer_interface::Execute(framesync_[ch], s, 1);

                        // Append the sample to the appropriate recording buffer
                        if (recording_) {
                            frame_buf_[ch].push_back(*s);
                        } else {
                            accum_buf_[ch].push_back(*s);
                        }
                    }

                    // --- State transitions (once per block, after all channels) ---

                    if (record_index > 0 && !recording_) {
                        // Snapshot trigger: trim every channel's history to the last record_index
                        // samples and use these as the initial contents of the snapshot buffer.
                        for (unsigned int ch = 0; ch < num_channels; ++ch) {
                            auto& src = accum_buf_[ch];
                            std::size_t keep = src.size() > record_index
                                               ? src.size() - record_index : 0;
                            frame_buf_[ch].assign(src.begin() + keep, src.end());
                            src.clear();
                        }
                        recording_ = true;

                    } else if (record_index == 0 && recording_) {
                        // Stop recording; frame_buf_ now holds the complete snapshot.
                        recording_ = false;
                        std::cout << "Recording stopped, snapshot ready with "
                                  << frame_buf_[0].size() << " samples." << std::endl;
                    }
                };

    /**
     * @brief  Set the phase-correction value for the specified channel by phi [rad] 
     * (increments the internal nco phase to phi)
     * 
     * @param channel_id Channel-ID
     */
    void SetNcoPhase(unsigned int channel_id, float phi)
                {
                    // Set the NCO phase
                    nco_crcf_set_phase(nco_[channel_id], phi);
                };

    /**
     * @brief  Increment the phase-correction value for the specified channel by dphi [rad] 
     * (increments the internal nco phase by dphi)
     * 
     * @param channel_id Channel-ID
     */
    void AdjustNcoPhase(unsigned int channel_id, float dphi)
                {
                    // Adjust the NCO phase
                    nco_crcf_adjust_phase(nco_[channel_id], dphi);
                };

    /**
     * @brief  Get the phase-correction value for the specified channel [rad] (get NCO phase)
     * 
     * @param channel_id Channel-ID
     */
    float GetNcoPhase(unsigned int channel_id)
                {
                    // Get the NCO phase
                    return nco_crcf_get_phase(nco_[channel_id]);
                };

    /**
     * @brief Get the samples of the last frame of the specified channel
     * 
     * The function is called within the user defined callback function 
     * 
     * @param channel_id Channel-ID
     * @param X Vector to store the Samples on
     */
    void GetFrameSamps(unsigned int       channel_id,
                std::vector<Sample_t>*    X){
        unsigned int i = 0;
        unsigned int ret_val = 1;
        Sample_t s;
        while (ret_val != 0) {
            ret_val = synchronizer_interface::GetFrameSamp(framesync_[channel_id], &s, i);
            if (ret_val == 1 && s != Sample_t{0.0f, 0.0f}) X->push_back(s);   // Store valid non-zero Sample
            i++;                                 // Continue with next buffer position
        };
    };

    
    /**
     * @brief Get the symbols of the last frame of the specified channel
     * 
     * The function is called within the user defined callback function 
     * 
     * @param channel_id Channel-ID
     * @param X Vector to store the Symbols on
     */
    void GetFrameSyms(unsigned int channel_id,
                std::vector<Symbol_t>*    X){
        unsigned int i = 0;
        unsigned int ret_val = 1;
        Symbol_t s;
        while (ret_val != 0) {
            ret_val = synchronizer_interface::GetFrameSym(framesync_[channel_id], &s, i);
            if (ret_val == 1 && s != Symbol_t{0.0f, 0.0f}) X->push_back(s);   // Store valid non-zero symbol
            i++;                                 // Continue with next buffer position
        };
    };

    /**
     * @brief Get the time-aligned multi-channel frame snapshot assembled by Execute().
     *
     * Returns a 2-D buffer indexed as [channel_id][sample_index]. Each inner vector holds the
     * NCO-corrected samples recorded for that channel during the last recording window.
     * The buffer is populated once a recording cycle completes (record_index transitions back to 0).
     * Call ClearMultiChannelFrameSamps() after consuming the data to prepare for the next cycle.
     *
     * @return const reference to the 2-D snapshot buffer
     */
    const std::vector<std::vector<Sample_t>>& GetMultiChannelFrameSamps() const {
        return frame_buf_;
    };

    /**
     * @brief Clear the multi-channel frame snapshot buffer.
     * Must be called by SyncWorker after the snapshot has been consumed so that the buffer is
     * ready for the next detection cycle.
     */
    void ClearMultiChannelFrameSamps() {
        for (auto& buf : frame_buf_) buf.clear();
    };

    /**
     * @brief Returns true while a recording window is active (record_index > 0 was last seen).
     */
    bool IsRecording() const { return recording_; };

private:
    /**
     * @brief Pointer to first synchronizer instance in the array of synchronizer instances for all channels
     * 
     */
    synchronizer_interface::SynchronizerType* framesync_; 

    /**
     * @brief Pointer to first NCO instance in the array of NCO instances
     * 
     */
    nco_crcf* nco_; 


    /**
     * @brief Synchronizer initialization parameters
     *
     */
    CreateParams_t params_;

    /**
     * @brief Callback wrapper storing the generic handler and real userdata.
     * Passed to each synchronizer instance as userdata.
     */
    CallbackWrapper cb_wrapper_;

    /**
     * @brief Per-channel rolling accumulation buffer (searching mode).
     * Holds NCO-corrected samples since the last recording cycle ended.
     * Trimmed and moved into frame_buf_ when a recording window is opened.
     */
    Samples_2dim_t accum_buf_;

    /**
     * @brief Per-channel snapshot buffer (recording mode).
     * Indexed as [channel_id][sample_index].
     * Initialized with trimmed history on recording start, then extended sample-by-sample until
     * recording stops. Read via GetMultiChannelFrameSamps().
     */
    Samples_2dim_t frame_buf_;

    /**
     * @brief True while a recording window is active.
     */
    bool recording_ = false;

};

#endif // MULTISYNC_H
