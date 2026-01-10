/**
 * @file synctraits.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief This file contains the abstract definition of the interface between the MultiSync class and different Liquid-DSP synchronizer types.
 * 
 * The MultiSync class utilizes the abstract SyncTraits struct that defines the Liquid-DSP interface for 
 * the specified synchronizer type. SyncTraits defines the C-function call of the Liquid-DSP synchronizer 
 * corresponding to the C++ functions used in MultiSync (Create, Reset, Execute, Destroy, GetFrameSamps).
 * 
 * Enabling MultiSync for different synchronizer types is achieved by specializing the SyncTraits-template for the required synchronizer type. 
 * 
 * 
 * Liquid's DSP-modules are based on https://github.com/jgaeddert/liquid-dsp (Copyright (c) 2007 - 2016 Joseph Gaeddert).
 * 
 * @version 0.1
 * @date 2026-01-10
 * 
 * @copyright Copyright (c) 2025
 */

#ifndef SYNCTRAITS_H
#define SYNCTRAITS_H

#include <complex>
#include <cmath>
#include <cassert>
#include <concepts>
#include <liquid.h>

/**
 * @brief Concept to check that the SyncTraits Template-specialization provides the required interface for MultiSync.
 * SyncTraits-Template specializations define a standard interface for different Liquid-DSP synchronizer types. 
 * 
 * @tparam SyncTraitsSpecification SyncTraits specialization to check
 */
template<typename SyncTraitsSpecification>
concept SyncTraitsConcept = requires(
    // local variables to check function signatures and typedefinitions
    typename SyncTraitsSpecification::SynchronizerType synchronizer,            // Frame Synchronizer
    typename SyncTraitsSpecification::CallbackType callback,                    // Callback function 
    typename SyncTraitsSpecification::CreateParams createParams,                // Parameters for synchronizer create function

    std::complex<float>* x,                 // Pointer to input samples array for Execute function
    unsigned int n,                         // Number of input samples to read for Execute function
    std::vector<std::complex<float>>* X     // Vector to store samples for GetFrameSamps function
) {
    // Check function signatures
    { SyncTraitsSpecification::Create(createParams, callback, nullptr) } -> std::same_as<typename SyncTraitsSpecification::SynchronizerType>;
    { SyncTraitsSpecification::Reset(synchronizer) } -> std::same_as<void>;
    { SyncTraitsSpecification::Execute(synchronizer, x, n) } -> std::same_as<int>;
    { SyncTraitsSpecification::Destroy(synchronizer) } -> std::same_as<void>;
    { SyncTraitsSpecification::GetFrameSamps(synchronizer, X) } -> std::same_as<void>;
};

/**
 * @brief Abstract struct to define the Liquid-DSP interface for different synchronizer types
 * 
 * @tparam Synchronizer type
 */
template<typename T>
struct SyncTraits {
    static_assert(sizeof(T) == 0, "Liquid-Functions not defined for this synchronizer type");
};

// ------------------------------------------------------------------------------------------
// -------------------- Specializations for different synchronizer types --------------------
// ------------------------------------------------------------------------------------------

// -------------------- OFDM Frame Synchronizer --------------------
/**
 * @brief SyncTraits specialization for the OFDM frame synchronizer.
 * Define the functions (Create, Reset, Execute, Destroy, GetFrameSamps) for the OFDM frame synchronizer
 * 
 * @tparam  Synchronizer type
 */
template<>
struct SyncTraits<ofdmframesync> {

    /**
     * @brief Define the type of the synchronizer
     * 
     */
    using SynchronizerType = ofdmframesync;

    /**
     * @brief Define the type of the callback function used in the OFDM frame synchronizer
     * 
     */
    using CallbackType = ofdmframesync_callback;

    /**
     * @brief Define the Parameters for the OFDM frame synchronizer Create function
     * 
     */
    struct CreateParams {
        unsigned int M;           // number of subcarriers
        unsigned int cp_len;      // cyclic prefix length
        unsigned int taper_len;   // taper length
        unsigned char * p;        // modulation scheme
    };

    /**
     * @brief Wrapper function to create an OFDM frame synchronizer
     * 
     * @param params Synchronizer parameters
     * @param callback Callback function
     * @param userdata User-defined data structure
     * @return SynchronizerType Created synchronizer instance
     */
    static SynchronizerType Create(const CreateParams& params, ofdmframesync_callback callback, void* userdata) 
    {
        return ofdmframesync_create(params.M, params.cp_len, params.taper_len, params.p, callback, userdata);
    };

    /**
     * @brief Wrapper function to reset an OFDM frame synchronizer
     * 
     * @param fs 
     */
    static void Reset(SynchronizerType fs) 
    {
        ofdmframesync_reset(fs);
    };

    /**
     * @brief Wrapper function to execute an OFDM frame synchronizer
     * 
     * @param fs Pointer to the synchronizer
     * @param x Pointer to the input samples array
     * @param n Number of input samples to read
     * @return int Result of the execution
     */
    static int Execute(SynchronizerType fs, std::complex<float>* x, unsigned int n) 
    {
        return ofdmframesync_execute(fs, x, n);
    };

    /**
     * @brief Wrapper function to destroy an OFDM frame synchronizer
     * 
     * @param fs Pointer to the synchronizer
     */
    static void Destroy(SynchronizerType fs) 
    {
        ofdmframesync_destroy(fs);
    };

    /**
     * @brief Wrapper function to get samples of the last frame received by the OFDM frame synchronizer.
     * Here the CFR (Channel Frequency Response is used insted of time-domain samples) (no difference in correlation matrix). 
     * 
     * @param fs Pointer to the synchronizer
     * @param X Vector to store the samples on
     */
    static void GetFrameSamps(SynchronizerType fs, std::vector<std::complex<float>>* X) 
    {
        unsigned int fft_size = ofdmframesync_get_fft_size(fs);
        X->resize(fft_size);
        ofdmframesync_get_cfr(fs, X->data(), fft_size);
    };
};

/**
 * @brief Define typename for OFDM synchronizer interface
 * 
 */
using ofdmframesync_iface = SyncTraits<ofdmframesync>;

// -------------------- Flexible Single-Carrier Frame Synchronizer --------------------

/**
 * @brief SyncTraits specialization for the flexible single-carrier frame synchronizer flexframesync.
 * Define the functions (Create, Reset, Execute, Destroy, GetFrameSamps) for the flexible frame synchronizer
 * 
 * @tparam  Synchronizer type
 */

template<>
struct SyncTraits<flexframesync> {

    /**
     * @brief Define the type of the synchronizer
     * 
     */
    using SynchronizerType = flexframesync;

    /**
     * @brief Define the type of the callback function used in the frame synchronizer
     * 
     */
    using CallbackType = framesync_callback;

    /**
     * @brief Define the Parameters for the frame synchronizer Create function
     * 
     */
    struct CreateParams {

    };

    /**
     * @brief Wrapper function to create a frame synchronizer
     * 
     * @param params Synchronizer parameters
     * @param callback Callback function
     * @param userdata User-defined data structure
     * @return SynchronizerType Created synchronizer instance
     */
    static SynchronizerType Create(const CreateParams& params, CallbackType callback, void* userdata) 
    {
        return flexframesync_create(callback, userdata);
    };

    /**
     * @brief Wrapper function to reset a frame synchronizer
     * 
     * @param fs 
     */
    static void Reset(SynchronizerType fs) 
    {
        flexframesync_reset(fs);
    };

    /**
     * @brief Wrapper function to execute a frame synchronizer
     * 
     * @param fs Pointer to the synchronizer
     * @param x Pointer to the input samples array
     * @param n Number of input samples to read
     * @return int Result of the execution
     */
    static int Execute(SynchronizerType fs, std::complex<float>* x, unsigned int n) 
    {
        return flexframesync_execute(fs, x, n);
    };

    /**
     * @brief Wrapper function to destroy a frame synchronizer
     * 
     * @param fs Pointer to the synchronizer
     */
    static void Destroy(SynchronizerType fs) 
    {
        flexframesync_destroy(fs);
    };

    /**
     * @brief Wrapper function to get samples of the last frame received by the frame synchronizer
     * 
     * @param fs Pointer to the synchronizer
     * @param X Vector to store the samples on
     */
    static void GetFrameSamps(SynchronizerType fs, std::vector<std::complex<float>>* X) 
    {
        // X->resize(fft_size);
        // flexframesync_get_frame_samps(fs, X->data(), fft_size); // Currently not implemented for flexframesync (t.b.d.) !!!
    };
};

/**
 * @brief Define typename for flexible frame synchronizer interface
 * 
 */
using flexframesync_iface = SyncTraits<flexframesync>;

#endif // SYNCTRAITS_H