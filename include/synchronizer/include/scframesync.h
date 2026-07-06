/**
 * @file scframesync.h
 * @author Felix Schuelke (flxscode@gmail.com)
 *
 * @brief Generic single-carrier frame synchronizer to detect waveforms with a
 * known preamble / synchronization sequence (e.g. GPS L1 C/A, DVB-S2 PLHEADER).
 *
 * The synchronizer is derived from liquid-dsp's flexframesync and exposes a
 * C-API wrapped by the SyncTraits<scframesync> specialization in synctraits.h.
 * All standard-dependent parameters (preamble symbols, pulse shape, samples
 * per symbol, detection threshold, carrier offset search range, payload
 * length) are passed in via scframesync_config_t on create; standard-specific
 * preset initializers are provided in sc_standards.h.
 *
 * Detection uses liquid's qdetector_cccf (FFT-based cross-correlation against
 * the pulse-shaped preamble, estimating timing, carrier frequency/phase offset
 * and channel gain), followed by a polyphase matched-filter bank (firpfb_crcf)
 * for fractional timing correction and decimation to symbol rate. The header/
 * payload decoding chain of flexframesync (qpacketmodem, qpilotsync, FEC) is
 * dropped: after the preamble, a configurable number of payload symbols is
 * captured and the callback is invoked once per detected frame.
 *
 * Liquid's DSP-modules are based on https://github.com/jgaeddert/liquid-dsp (Copyright (c) 2007 - 2016 Joseph Gaeddert).
 *
 * @version 0.1
 * @date 2026-07-06
 *
 * @copyright Copyright (c) 2026
 */

#ifndef SCFRAMESYNC_H
#define SCFRAMESYNC_H

#include <liquid.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// single-carrier frame synchronizer
//
typedef struct scframesync_s * scframesync;

// synchronizer configuration; all fields differ between standards and are
// copied by scframesync_create()
typedef struct {
    liquid_float_complex * preamble;  // known preamble symbols (symbol rate), [size: preamble_len x 1]
    unsigned int preamble_len;        // number of preamble symbols
    int ftype;                        // pulse shape (root-Nyquist LIQUID_FIRFILT_*, e.g. LIQUID_FIRFILT_RRC)
    unsigned int k;                   // samples per symbol (>= 2, qdetector requirement)
    unsigned int m;                   // pulse-shape filter delay [symbols]
    float beta;                       // pulse-shape excess bandwidth / roll-off factor
    float threshold;                  // detection threshold (0..1, normalized correlation)
    float dphi_max;                   // carrier offset search range [radians/sample] (0: qdetector default)
    unsigned int payload_sym_len;     // number of payload symbols captured after the preamble
} scframesync_config_t;

// create single-carrier framing synchronizer object
//  _config     :   standard-dependent synchronizer configuration (copied)
//  _callback   :   user-defined callback function (framesync_callback signature)
//  _userdata   :   user-defined data pointer
scframesync scframesync_create(const scframesync_config_t * _config,
                               framesync_callback _callback,
                               void *             _userdata);
int scframesync_destroy(scframesync _q);
int scframesync_print(scframesync _q);
int scframesync_reset(scframesync _q);
int scframesync_is_frame_open(scframesync _q);
int scframesync_execute(scframesync _q,
                        liquid_float_complex * _x,
                        unsigned int _n);

// query methods
float scframesync_get_rssi(scframesync _q);     // received signal strength indication
float scframesync_get_cfo(scframesync _q);      // carrier offset estimate [radians/sample]
unsigned int scframesync_get_frame_len(scframesync _q); // Get length of detected frame (received samples since detection) in samples
unsigned int scframesync_get_sym(scframesync _q,        // received symbol buffer (preamble, then payload)
                          liquid_float_complex * _x,
                          unsigned int _pos);

#ifdef __cplusplus
}
#endif

#endif // SCFRAMESYNC_H
