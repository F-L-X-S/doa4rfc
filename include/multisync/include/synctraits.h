/**
 * @file synctraits.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief This file contains the abstract definition of the interface between the MultiSync class and different Liquid-DSP synchronizer types.
 * 
 * The MultiSync class utilizes the abstract SyncTraits struct that defines the Liquid-DSP interface for 
 * the specified synchronizer type. SyncTraits defines the C-function call of the Liquid-DSP synchronizer 
 * corresponding to the C++ functions used in MultiSync (Create, Reset, Execute, Destroy, GetFrameLen, GetFrameSyms).
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
#include <doa4rfc.h>

using namespace doa4rfc;

/**
 * @brief Generic handler function type for synchronizer callbacks.
 * SyncTraits specializations forward the C callback to this generic handler via CallbackWrapper.
 */
using GenericCallback_t = int(*)(void*);

/**
 * @brief Wrapper struct stored as userdata in liquid-dsp synchronizers.
 * The SyncTraits C callback extracts this wrapper and calls the generic handler.
 */
struct CallbackWrapper {
    GenericCallback_t handler;   // generic handler (e.g. SyncWorker::callback)
    void* userdata;              // real userdata (e.g. SyncWorker::cb_data_)
};

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
    typename SyncTraitsSpecification::CreateParams_t createParams,              // Parameters for synchronizer create function

    unsigned int n,                         // Number of input samples to read for Execute function
    Sample_t* x,                            // Pointer to Sample
    Symbol_t* y,                            // Pointer to Symbol
    unsigned int pos                        // Position Index to read fromGetFrameSym function
) {
    // Check function signatures
    { SyncTraitsSpecification::Create(createParams, (CallbackWrapper*)nullptr) } -> std::same_as<typename SyncTraitsSpecification::SynchronizerType>;
    { SyncTraitsSpecification::Reset(synchronizer) } -> std::same_as<void>;
    { SyncTraitsSpecification::Execute(synchronizer, x, n) } -> std::same_as<int>;
    { SyncTraitsSpecification::Destroy(synchronizer) } -> std::same_as<void>;
    { SyncTraitsSpecification::GetFrameLen(synchronizer) } -> std::same_as<unsigned int>;
    { SyncTraitsSpecification::GetFrameSym(synchronizer, y, pos) } -> std::same_as<unsigned int>;
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
 * Defines the functions (Create, Reset, Execute, Destroy, GetFrameLen, GetFrameSyms) for the OFDM frame synchronizer.
 * 
 * ofdmframesync documentation: https://liquidsdr.org/doc/ofdmflexframe/
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
     * @brief Define the Parameters for the OFDM frame synchronizer Create function
     * 
     */
    struct CreateParams_t {
        unsigned int M;           // number of subcarriers
        unsigned int cp_len;      // cyclic prefix length
        unsigned int taper_len;   // taper length
        unsigned char * p;        // modulation scheme
    };

    /**
     * @brief C-compatible callback matching ofdmframesync_callback signature.
     * Extracts CallbackWrapper from userdata and forwards to generic handler.
     */
    static int Callback(liquid_float_complex * _X, unsigned char * _p,
                         unsigned int _M, void * _userdata)
    {
        auto* w = static_cast<CallbackWrapper*>(_userdata);
        return w->handler(w->userdata);
    };

    /**
     * @brief Wrapper function to create an OFDM frame synchronizer
     *
     * @param params Synchronizer parameters
     * @param wrapper CallbackWrapper containing generic handler and real userdata
     * @return SynchronizerType Created synchronizer instance
     */
    static SynchronizerType Create(const CreateParams_t& params, CallbackWrapper* wrapper)
    {
        return ofdmframesync_create(params.M, params.cp_len, params.taper_len, params.p, Callback, wrapper);
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
    static int Execute(SynchronizerType fs, Sample_t* x, unsigned int n) 
    {
        return ofdmframesync_execute(fs, reinterpret_cast<liquid_float_complex*>(x), n);
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
     * @brief Wrapper function to get sample of the last frame received by the frame synchronizer.
     * 
     * The function is called within the user defined callback function 
     * 
     * @param fs Pointer to the synchronizer
     * @param _x buffer to store the sample on 
     * @param _pos Index of buffer position to read
     */
    static unsigned int GetFrameLen(SynchronizerType fs) 
    {
        return ofdmframesync_get_frame_len(fs);
    };

    /**
     * @brief Wrapper function to get symbol of the last frame received by the frame synchronizer. 
     * 
     * The function is called within the user defined callback function 
     * 
     * @param fs Pointer to the synchronizer
     * @param _x buffer to store the symbol on 
     * @param _pos Index of buffer position to read
     */
    static unsigned int GetFrameSym(SynchronizerType fs, Symbol_t* _x, unsigned int _pos) 
    {
        liquid_float_complex* _x_liquid = liquid_conv::Ptr(_x);
        return ofdmframesync_get_sym(fs, _x_liquid, _pos);
    };
};

/**
 * @brief Define typename for OFDM synchronizer interface
 * 
 */
using ofdmframesync_iface = SyncTraits<ofdmframesync>;


// -------------------- Flexible OFDM Frame Synchronizer --------------------

/**
 * @brief SyncTraits specialization for the flexible ofdm frame synchronizer ofdmflexframesync.
 * Defines the functions (Create, Reset, Execute, Destroy, GetFrameLen, GetFrameSyms) for the flexible frame synchronizer.
 * 
 * flexframesync documentation: https://liquidsdr.org/doc/tutorial-ofdmflexframe/
 * 
 * @tparam  Synchronizer type
 */

template<>
struct SyncTraits<ofdmflexframesync> {

    /**
     * @brief Define the type of the synchronizer
     * 
     */
    using SynchronizerType = ofdmflexframesync;

    /**
     * @brief Define the Parameters for the frame synchronizer Create function
     * 
     */
    struct CreateParams_t {
        unsigned int M;           // number of subcarriers
        unsigned int cp_len;      // cyclic prefix length
        unsigned int taper_len;   // taper length
        unsigned char * p;        // modulation scheme
    };

    /**
     * @brief C-compatible callback matching framesync_callback signature.
     * Extracts CallbackWrapper from userdata and forwards to generic handler.
     */
    static int Callback(unsigned char *  _header,
                        int              _header_valid,
                        unsigned char *  _payload,
                        unsigned int     _payload_len,
                        int              _payload_valid,
                        framesyncstats_s _stats,
                        void *           _userdata)
    {
        auto* w = static_cast<CallbackWrapper*>(_userdata);
        return w->handler(w->userdata);
    };

    /**
     * @brief Wrapper function to create a frame synchronizer
     *
     * @param params Synchronizer parameters
     * @param wrapper CallbackWrapper containing generic handler and real userdata
     * @return SynchronizerType Created synchronizer instance
     */
    static SynchronizerType Create(const CreateParams_t& params, CallbackWrapper* wrapper)
    {
        return ofdmflexframesync_create(params.M, params.cp_len, params.taper_len, params.p, Callback, wrapper);
    };

    /**
     * @brief Wrapper function to reset a frame synchronizer
     * 
     * @param fs 
     */
    static void Reset(SynchronizerType fs) 
    {
        ofdmflexframesync_reset(fs);
    };

    /**
     * @brief Wrapper function to execute a frame synchronizer
     * 
     * @param fs Pointer to the synchronizer
     * @param x Pointer to the input samples array
     * @param n Number of input samples to read
     * @return int Result of the execution
     */
    static int Execute(SynchronizerType fs, Sample_t* x, unsigned int n) 
    {
        return ofdmflexframesync_execute(fs, reinterpret_cast<liquid_float_complex*>(x), n);
    };

    /**
     * @brief Wrapper function to destroy a frame synchronizer
     * 
     * @param fs Pointer to the synchronizer
     */
    static void Destroy(SynchronizerType fs) 
    {
        ofdmflexframesync_destroy(fs);
    };

    /**
     * @brief Wrapper function to get sample of the last frame received by the frame synchronizer.
     * 
     * The function is called within the user defined callback function 
     * 
     * @param fs Pointer to the synchronizer
     * @param _x buffer to store the sample on 
     * @param _pos Index of buffer position to read
     */
    static unsigned int GetFrameLen(SynchronizerType fs) 
    {
        return ofdmflexframesync_get_frame_len(fs);
    };

    /**
     * @brief Wrapper function to get symbol of the last frame received by the frame synchronizer. 
     * 
     * The function is called within the user defined callback function 
     * 
     * @param fs Pointer to the synchronizer
     * @param _x buffer to store the symbol on 
     * @param _pos Index of buffer position to read
     */
    static unsigned int GetFrameSym(SynchronizerType fs, Symbol_t* _x, unsigned int _pos) 
    {
        liquid_float_complex* _x_liquid = liquid_conv::Ptr(_x);
        return ofdmflexframesync_get_sym(fs, _x_liquid, _pos);
    };
};

/**
 * @brief Define typename for flexible frame synchronizer interface
 * 
 */
using ofdmflexframesync_iface = SyncTraits<ofdmflexframesync>;


// -------------------- Flexible Single-Carrier Frame Synchronizer --------------------

/**
 * @brief SyncTraits specialization for the flexible single-carrier frame synchronizer flexframesync.
 * Defines the functions (Create, Reset, Execute, Destroy, GetFrameLen, GetFrameSyms) for the flexible frame synchronizer.
 * 
 * flexframesync documentation: https://liquidsdr.org/doc/flexframe/
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
     * @brief Define the Parameters for the frame synchronizer Create function
     * 
     */
    struct CreateParams_t {

    };

    /**
     * @brief C-compatible callback matching framesync_callback signature.
     * Extracts CallbackWrapper from userdata and forwards to generic handler.
     */
    static int Callback(unsigned char *  _header,
                        int              _header_valid,
                        unsigned char *  _payload,
                        unsigned int     _payload_len,
                        int              _payload_valid,
                        framesyncstats_s _stats,
                        void *           _userdata)
    {
        auto* w = static_cast<CallbackWrapper*>(_userdata);
        return w->handler(w->userdata);
    };

    /**
     * @brief Wrapper function to create a frame synchronizer
     *
     * @param params Synchronizer parameters
     * @param wrapper CallbackWrapper containing generic handler and real userdata
     * @return SynchronizerType Created synchronizer instance
     */
    static SynchronizerType Create(const CreateParams_t& params, CallbackWrapper* wrapper)
    {
        return flexframesync_create(Callback, wrapper);
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
    static int Execute(SynchronizerType fs, Sample_t* x, unsigned int n) 
    {
        return flexframesync_execute(fs, reinterpret_cast<liquid_float_complex*>(x), n);
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
     * @brief Wrapper function to get length in samples of the last frame received by the frame synchronizer.
     * 
     * The function is called within the user defined callback function 
     * 
     * @param fs Pointer to the synchronizer
     */
    static unsigned int GetFrameLen(SynchronizerType fs) 
    {
        return flexframesync_get_frame_len(fs);
    };

    /**
     * @brief Wrapper function to get symbol of the last frame received by the frame synchronizer. 
     * 
     * The function is called within the user defined callback function 
     * 
     * @param fs Pointer to the synchronizer
     * @param _x buffer to store the symbol on 
     * @param _pos Index of buffer position to read
     */
    static unsigned int GetFrameSym(SynchronizerType fs, Symbol_t* _x, unsigned int _pos) 
    {
        liquid_float_complex* _x_liquid = liquid_conv::Ptr(_x);
        return flexframesync_get_sym(fs, _x_liquid, _pos);
    };
};

/**
 * @brief Define typename for flexible frame synchronizer interface
 * 
 */
using flexframesync_iface = SyncTraits<flexframesync>;


// -------------------- IEEE802.11n OFDM Frame Synchronizer --------------------

// custom IEEE802.11n frame synchronizer (C-implementation in multisync/src/wlanframesync.c)
#include "wlanframesync.h"

/**
 * @brief SyncTraits specialization for the IEEE802.11n OFDM Frame Synchronizer wlanframesync.
 * Defines the functions (Create, Reset, Execute, Destroy, GetFrameLen, GetFrameSyms) for the IEEE802.11n OFDM Frame Synchronizer.
 *
 * The standard-dependent parameters (FFT size, cyclic prefix length, subcarrier
 * allocation, training sequences, pilot pattern) are passed in via the
 * wlanframesync_config_t held by CreateParams_t; preset helpers for the
 * IEEE 802.11n HT-mixed configuration are provided in wlanframesync.h.
 *
 * @tparam  Synchronizer type
 */

template<>
struct SyncTraits<wlanframesync> {

    /**
     * @brief Define the type of the synchronizer
     *
     */
    using SynchronizerType = wlanframesync;

    /**
     * @brief Define the Parameters for the frame synchronizer Create function
     *
     */
    struct CreateParams_t {
        wlanframesync_config_t config;   // standard-dependent synchronizer configuration (arrays copied on create)
    };

    /**
     * @brief C-compatible callback matching framesync_callback signature.
     * Extracts CallbackWrapper from userdata and forwards to generic handler.
     */
    static int Callback(unsigned char *  _header,
                        int              _header_valid,
                        unsigned char *  _payload,
                        unsigned int     _payload_len,
                        int              _payload_valid,
                        framesyncstats_s _stats,
                        void *           _userdata)
    {
        auto* w = static_cast<CallbackWrapper*>(_userdata);
        return w->handler(w->userdata);
    };

    /**
     * @brief Wrapper function to create a frame synchronizer
     *
     * @param params Synchronizer parameters
     * @param wrapper CallbackWrapper containing generic handler and real userdata
     * @return SynchronizerType Created synchronizer instance
     */
    static SynchronizerType Create(const CreateParams_t& params, CallbackWrapper* wrapper)
    {
        return wlanframesync_create(&params.config, Callback, wrapper);
    };

    /**
     * @brief Wrapper function to reset a frame synchronizer
     *
     * @param fs
     */
    static void Reset(SynchronizerType fs)
    {
        wlanframesync_reset(fs);
    };

    /**
     * @brief Wrapper function to execute a frame synchronizer
     *
     * @param fs Pointer to the synchronizer
     * @param x Pointer to the input samples array
     * @param n Number of input samples to read
     * @return int Result of the execution
     */
    static int Execute(SynchronizerType fs, Sample_t* x, unsigned int n)
    {
        return wlanframesync_execute(fs, reinterpret_cast<liquid_float_complex*>(x), n);
    };

    /**
     * @brief Wrapper function to destroy a frame synchronizer
     *
     * @param fs Pointer to the synchronizer
     */
    static void Destroy(SynchronizerType fs)
    {
        wlanframesync_destroy(fs);
    };

    /**
     * @brief Wrapper function to get sample of the last frame received by the frame synchronizer.
     *
     * The function is called within the user defined callback function
     *
     * @param fs Pointer to the synchronizer
     * @param _x buffer to store the sample on
     * @param _pos Index of buffer position to read
     */
    static unsigned int GetFrameLen(SynchronizerType fs)
    {
        return wlanframesync_get_frame_len(fs);
    };

    /**
     * @brief Wrapper function to get symbol of the last frame received by the frame synchronizer.
     *
     * The function is called within the user defined callback function
     *
     * @param fs Pointer to the synchronizer
     * @param _x buffer to store the symbol on
     * @param _pos Index of buffer position to read
     */
    static unsigned int GetFrameSym(SynchronizerType fs, Symbol_t* _x, unsigned int _pos)
    {
        liquid_float_complex* _x_liquid = liquid_conv::Ptr(_x);
        return wlanframesync_get_sym(fs, _x_liquid, _pos);
    };
};

/**
 * @brief Define typename for IEEE802.11n frame synchronizer interface
 *
 */
using wlanframesync_iface = SyncTraits<wlanframesync>;


// -------------------- Single-Carrier Frame Synchronizer --------------------

// custom single-carrier frame synchronizer (C-implementation in synchronizer/src/scframesync.c)
#include "scframesync.h"

/**
 * @brief SyncTraits specialization for the single-carrier frame synchronizer scframesync.
 * Defines the functions (Create, Reset, Execute, Destroy, GetFrameLen, GetFrameSyms) for the single-carrier frame synchronizer.
 *
 * The standard-dependent parameters (preamble symbols, pulse shape, samples
 * per symbol, detection threshold, carrier offset search range, payload
 * length) are passed in via the scframesync_config_t held by CreateParams_t;
 * preset helpers (e.g. GPS L1 C/A, DVB-S2 SOF) are provided in sc_standards.h.
 *
 * @tparam  Synchronizer type
 */

template<>
struct SyncTraits<scframesync> {

    /**
     * @brief Define the type of the synchronizer
     *
     */
    using SynchronizerType = scframesync;

    /**
     * @brief Define the Parameters for the frame synchronizer Create function
     *
     */
    struct CreateParams_t {
        scframesync_config_t config;   // standard-dependent synchronizer configuration (arrays copied on create)
    };

    /**
     * @brief C-compatible callback matching framesync_callback signature.
     * Extracts CallbackWrapper from userdata and forwards to generic handler.
     */
    static int Callback(unsigned char *  _header,
                        int              _header_valid,
                        unsigned char *  _payload,
                        unsigned int     _payload_len,
                        int              _payload_valid,
                        framesyncstats_s _stats,
                        void *           _userdata)
    {
        auto* w = static_cast<CallbackWrapper*>(_userdata);
        return w->handler(w->userdata);
    };

    /**
     * @brief Wrapper function to create a frame synchronizer
     *
     * @param params Synchronizer parameters
     * @param wrapper CallbackWrapper containing generic handler and real userdata
     * @return SynchronizerType Created synchronizer instance
     */
    static SynchronizerType Create(const CreateParams_t& params, CallbackWrapper* wrapper)
    {
        return scframesync_create(&params.config, Callback, wrapper);
    };

    /**
     * @brief Wrapper function to reset a frame synchronizer
     *
     * @param fs
     */
    static void Reset(SynchronizerType fs)
    {
        scframesync_reset(fs);
    };

    /**
     * @brief Wrapper function to execute a frame synchronizer
     *
     * @param fs Pointer to the synchronizer
     * @param x Pointer to the input samples array
     * @param n Number of input samples to read
     * @return int Result of the execution
     */
    static int Execute(SynchronizerType fs, Sample_t* x, unsigned int n)
    {
        return scframesync_execute(fs, reinterpret_cast<liquid_float_complex*>(x), n);
    };

    /**
     * @brief Wrapper function to destroy a frame synchronizer
     *
     * @param fs Pointer to the synchronizer
     */
    static void Destroy(SynchronizerType fs)
    {
        scframesync_destroy(fs);
    };

    /**
     * @brief Wrapper function to get length in samples of the last frame received by the frame synchronizer.
     *
     * The function is called within the user defined callback function
     *
     * @param fs Pointer to the synchronizer
     */
    static unsigned int GetFrameLen(SynchronizerType fs)
    {
        return scframesync_get_frame_len(fs);
    };

    /**
     * @brief Wrapper function to get symbol of the last frame received by the frame synchronizer.
     *
     * The function is called within the user defined callback function
     *
     * @param fs Pointer to the synchronizer
     * @param _x buffer to store the symbol on
     * @param _pos Index of buffer position to read
     */
    static unsigned int GetFrameSym(SynchronizerType fs, Symbol_t* _x, unsigned int _pos)
    {
        liquid_float_complex* _x_liquid = liquid_conv::Ptr(_x);
        return scframesync_get_sym(fs, _x_liquid, _pos);
    };
};

/**
 * @brief Define typename for single-carrier frame synchronizer interface
 *
 */
using scframesync_iface = SyncTraits<scframesync>;


#endif // SYNCTRAITS_H