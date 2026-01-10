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
 * corresponding to the C++ functions used in MultiSync (Create, Reset, Execute, Destroy, GetCfr).
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
#include <liquid.h>
#include <multisync/synctraits.h>

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
    using ParamsType   = typename synchronizer_interface::CreateParams;

    /**
     * @brief Callback function type used by the chosen synchronizer type
     * 
     */
    using CallbackType = typename synchronizer_interface::CallbackType;


    /**
     * @brief Construct a new MultiSync object with the specified number of channels and given synchronizer parameters.
     * Synchronizers will call the user-defined callback function with the user-defined data structure when receiving a frame.
     * 
     * @param _num_channels number of channels
     * @param _synchronizer_params synchronizer parameters
     * @param _userdata user-defined data structure array
     * @param _callback user-defined callback function
     */
    MultiSync(unsigned int           num_channels,
            const ParamsType&        synchronizer_params, 
            CallbackType             callback,
            void **                  userdata):
            num_channels_(num_channels), userdata_(userdata), callback_(callback)
            {
                // create synchronizer instances for all channels
                framesync_ = new synchronizer_interface::SynchronizerType[num_channels_];
                // create NCO instances for all channels
                nco_ = new nco_crcf[num_channels_];
                // initialize NCO and synchronizer instances 
                for (unsigned int i=0; i<num_channels; i++) {
                    userdata_[i]  = userdata[i];
                    framesync_[i] = synchronizer_interface::Create(synchronizer_params, callback_, userdata_[i]);
                    nco_[i] = nco_crcf_create(LIQUID_VCO);
                }
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
     * @brief Push samples into the specified channel's synchronizer object. 
     * The samples will be processed by the NCO first, which applies the phase correction to the samples.
     * @param channel_id Channel-ID
     * @param x pointer to the input samples
     * @param num_samples number of input samples to process
     */
    void Execute(unsigned int           channel_id,
                std::vector<std::complex<float>>* x)
                {   
                    nco_crcf_mix_block_up(nco_[channel_id],x->data(), x->data(),x->size());                 // Apply constant phase offset 
                    synchronizer_interface::Execute(framesync_[channel_id], x->data(), x->size());
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
     * @brief Get the Channel frequency response (CFR) of the specified channel
     * 
     * @param channel_id Channel-ID
     * @param X Vector to store the CFR (Channel Frequency Response) on
     */
    void GetCfr(unsigned int                         channel_id, 
                std::vector<std::complex<float>>*    X)
                {
                    synchronizer_interface::GetCfr(framesync_[channel_id], X);
                };


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
    ParamsType params_;

    /**
     * @brief Pointer to user-defined data structure array
     * 
     */
    void ** userdata_;               

    /**
     * @brief Callback function to call when a frame is detected by a synchronizer
     * 
     */
    CallbackType callback_;  

};

#endif // MULTISYNC_H