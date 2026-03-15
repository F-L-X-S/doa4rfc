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
                std::vector<Sample_t>* x)
                {   
                    // Apply constant phase offset 
                    nco_crcf_mix_block_up(nco_[channel_id],
                        reinterpret_cast<liquid_float_complex*>(x->data()), 
                        reinterpret_cast<liquid_float_complex*>(x->data()),
                        x->size());                 
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
            if (ret_val == 1) X->push_back(s);   // Store Sample 
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
            if (ret_val == 1) X->push_back(s);   // Store Sample 
            i++;                                 // Continue with next buffer position 
        };
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
    CreateParams_t params_;

    /**
     * @brief Callback wrapper storing the generic handler and real userdata.
     * Passed to each synchronizer instance as userdata.
     */
    CallbackWrapper cb_wrapper_;

};

#endif // MULTISYNC_H