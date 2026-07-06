/**
 * @file wlanframesync.h
 * @author Felix Schuelke (flxscode@gmail.com)
 *
 * @brief  IEEE 802.11n frame synchronizer to detect the 802.11n
 * HT-mixed legacy preamble (L-STF/L-LTF, 64-pt FFT at 20 MHz).
 *
 * The synchronizer is derived from liquid-dsp's ofdmframesync and exposes a
 * C-API wrapped by the SyncTraits<wlanframesync> specialization in synctraits.h.
 * All standard-dependent parameters (FFT size, cyclic prefix length, subcarrier
 * allocation, training sequences, pilot pattern) are passed in via
 * wlanframesync_config_t on create; standard-specific preset initializers
 * are provided in wlan_standards.h.
 *
 * Liquid's DSP-modules are based on https://github.com/jgaeddert/liquid-dsp (Copyright (c) 2007 - 2016 Joseph Gaeddert).
 *
 * @version 0.1
 * @date 2026-07-05
 *
 * @copyright Copyright (c) 2026
 */

#ifndef WLANFRAMESYNC_H
#define WLANFRAMESYNC_H

#include <liquid.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// WLAN OFDM frame (symbol) synchronizer
//
typedef struct wlanframesync_s * wlanframesync;

// synchronizer configuration; all fields differ between WLAN standards /
// channelizations and are copied by wlanframesync_create()
typedef struct {
    unsigned int M;                   // FFT size / total number of subcarriers
    unsigned int cp_len;              // payload-symbol guard interval [samples]
    unsigned char * p;                // subcarrier allocation (OFDMFRAME_SCTYPE_*), [size: M x 1]
    liquid_float_complex * stf_seq;   // freq-domain STF, zeros on inactive tones, [size: M x 1]
    unsigned int stf_period;          // time-domain STF periodicity [samples] (16 for 802.11 @ 20 MHz)
    liquid_float_complex * ltf_seq;   // freq-domain LTF, zeros on inactive tones, [size: M x 1]
    unsigned int ltf_count;           // number of repeated LTF training symbols (2 for L-LTF)
    const float * pilot_base;         // pilot base pattern, ascending k, [size: number of pilots in p x 1]
} wlanframesync_config_t;

// create WLAN framing synchronizer object
//  _config     :   standard-dependent synchronizer configuration (copied)
//  _callback   :   user-defined callback function (framesync_callback signature)
//  _userdata   :   user-defined data pointer
wlanframesync wlanframesync_create(const wlanframesync_config_t * _config,
                                   framesync_callback _callback,
                                   void *             _userdata);
int wlanframesync_destroy(wlanframesync _q);
int wlanframesync_print(wlanframesync _q);
int wlanframesync_reset(wlanframesync _q);
int wlanframesync_is_frame_open(wlanframesync _q);
int wlanframesync_execute(wlanframesync _q,
                          liquid_float_complex * _x,
                          unsigned int _n);

// query methods
float wlanframesync_get_rssi(wlanframesync _q);     // received signal strength indication
float wlanframesync_get_cfo(wlanframesync _q);      // carrier offset estimate
unsigned int wlanframesync_get_cfr(wlanframesync _q,        // channel frequency response
                            liquid_float_complex * _x,
                            unsigned int _pos);
unsigned int wlanframesync_get_frame_len(wlanframesync _q); // Get length of detected frame (received symbols since reset) in samples
unsigned int wlanframesync_get_sym(wlanframesync _q,        // received data symbol buffer
                            liquid_float_complex * _x,
                            unsigned int _pos);
unsigned int wlanframesync_get_fft_size(wlanframesync _q);  // size of fft
nco_crcf* wlanframesync_get_nco(wlanframesync _q);  // get nco object

// set methods
int wlanframesync_set_cfo(wlanframesync _q, float _cfo);  // set carrier offset estimate

// debugging
int wlanframesync_debug_enable(wlanframesync _q);
int wlanframesync_debug_disable(wlanframesync _q);
int wlanframesync_debug_print(wlanframesync _q, const char * _filename);

#ifdef __cplusplus
}
#endif

#endif // WLANFRAMESYNC_H
