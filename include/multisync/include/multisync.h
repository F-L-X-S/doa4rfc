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
template<SyncTraitsConcept synchronizer_interface>          
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
     * @param num_channels number of channels
     * @param synchronizer_params synchronizer parameters
     * @param handler generic callback handler invoked on frame detection
     * @param userdata user-defined data passed to handler
     */
    MultiSync(unsigned int           num_channels,
            const CreateParams_t&    synchronizer_params,
            GenericCallback_t        handler,
            void *                   userdata):
            num_channels_(num_channels), cb_wrapper_{handler, userdata}
            {
                // create synchronizer instances for all channels
                framesync_ = new synchronizer_interface::SynchronizerType[num_channels_];
                // create NCO instances for all channels
                nco_ = new nco_crcf[num_channels_];
                // initialize NCO and synchronizer instances
                for (unsigned int i=0; i<num_channels; i++) {
                    framesync_[i] = synchronizer_interface::Create(synchronizer_params, &cb_wrapper_);
                    nco_[i] = nco_crcf_create(LIQUID_VCO);
                }
                // initialize per-channel recording buffers
                accum_buf_.resize(num_channels_);
                frame_buf_.resize(num_channels_);
            }

    /**
     * @brief Destroy the MultiSync object
     * 
     */
    ~MultiSync()
            {
                // Call Liquid-DSP destroy functions
                for (unsigned int i = 0; i < num_channels_; i++){
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
                for (unsigned int i = 0; i < num_channels_; ++i) 
                    synchronizer_interface::Reset(framesync_[i]);
            };

    /**
     * @brief Push samples into the specified channel's synchronizer and manage the multi-channel
     * recording window.
     *
     * In normal operation (record_index == 0) every NCO-corrected sample is appended to an internal
     * per-channel accumulation buffer so that a temporally aligned snapshot can be assembled later.
     *
     * Recording lifecycle (driven by SyncWorker):
     *
     *   record_index == 0  →  searching mode: samples accumulate in accum_buf_[channel_id].
     *
     *   record_index > 0, first time seen  →  snapshot trigger:
     *       All channels' accum_buf_ are trimmed to their last record_index samples and copied into
     *       frame_buf_ (the output snapshot). accum_buf_ entries are then cleared and recording_
     *       is set to true. From this call onward, every new sample is appended directly to frame_buf_.
     *
     *   record_index > 0, recording_ already true  →  continue appending to frame_buf_.
     *
     *   record_index == 0 while recording_ == true  →  stop recording. frame_buf_ contains the
     *       complete time-aligned multi-channel snapshot, ready to be read via
     *       GetMultiChannelFrameSamps(). accum_buf_ is left empty and refills from the next call.
     *
     * @param channel_id   Channel-ID
     * @param x            Pointer to the input sample vector (modified in-place by NCO correction)
     * @param record_index 0 = searching mode; >0 = trigger/continue recording with this window size
     */
    void Execute(unsigned int            channel_id,
                 std::vector<Sample_t>*  x,
                 unsigned int            record_index = 0)
                {
                    // Apply constant phase offset
                    nco_crcf_mix_block_up(nco_[channel_id],
                        reinterpret_cast<liquid_float_complex*>(x->data()),
                        reinterpret_cast<liquid_float_complex*>(x->data()),
                        x->size());

                    // Process the full block through the synchronizer at once
                    synchronizer_interface::Execute(framesync_[channel_id], x->data(), x->size());

                    // Append the whole block to the appropriate buffer
                    if (recording_) {
                        frame_buf_[channel_id].insert(frame_buf_[channel_id].end(), x->begin(), x->end());
                    } else {
                        accum_buf_[channel_id].insert(accum_buf_[channel_id].end(), x->begin(), x->end());
                    }

                    // --- State transitions (evaluated after all samples in x are processed) ---

                    if (record_index > 0 && !recording_) {
                        // Snapshot trigger: trim every channel's history to the last record_index
                        // samples and use these as the initial contents of the snapshot buffer.
                        // Because SyncWorker feeds all channels in lockstep, each accum_buf_ holds
                        // the same number of samples, so the trim produces a time-aligned window.
                        for (unsigned int ch = 0; ch < num_channels_; ++ch) {
                            auto& src = accum_buf_[ch];
                            std::size_t keep = src.size() > record_index
                                               ? src.size() - record_index : 0;
                            frame_buf_[ch].assign(src.begin() + keep, src.end());
                            src.clear();    // accum_buf_ no longer needed while recording
                        }
                        recording_ = true;

                    } else if (record_index == 0 && recording_) {
                        // Stop recording; frame_buf_ now holds the complete snapshot.
                        recording_ = false;
                        std::cout << "Recording stopped, snapshot ready with " << frame_buf_[channel_id].size() << " samples." << std::endl;
                        // accum_buf_ is already empty (cleared at recording start); it will
                        // refill naturally as Execute() is called with record_index == 0.
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
     * @brief number of receiving channels 
     * 
     */
    unsigned int num_channels_;      

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
    std::vector<std::vector<Sample_t>> accum_buf_;

    /**
     * @brief Per-channel snapshot buffer (recording mode).
     * Indexed as [channel_id][sample_index].
     * Initialized with trimmed history on recording start, then extended sample-by-sample until
     * recording stops. Read via GetMultiChannelFrameSamps().
     */
    std::vector<std::vector<Sample_t>> frame_buf_;

    /**
     * @brief True while a recording window is active.
     */
    bool recording_ = false;

};

#endif // MULTISYNC_H