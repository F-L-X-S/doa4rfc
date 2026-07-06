/**
 * @file sc_standards.h
 * @author Felix Schuelke (flxscode@gmail.com)
 *
 * @brief Standard-specific preset initializers for the scframesync
 * configuration (scframesync_config_t).
 *
 * Each single-carrier standard is described by its known synchronization
 * sequence (preamble symbols at symbol rate) plus pulse-shape and detection
 * parameters. The helpers in this file fill caller-provided arrays with the
 * preamble symbols; the caller composes them into a scframesync_config_t.
 *
 * To support an additional standard (e.g. GPS L2C or DVB-S), add the
 * corresponding initializer functions here.
 *
 * @version 0.1
 * @date 2026-07-06
 *
 * @copyright Copyright (c) 2026
 */

#ifndef SC_STANDARDS_H
#define SC_STANDARDS_H

#include <liquid.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// GPS L1 C/A (IS-GPS-200, coarse/acquisition code on L1, 1.023 Mchip/s)
//
// The preamble is one full period of the satellite-specific 1023-chip Gold
// code as BPSK symbols; the "symbol" rate of the synchronizer is the chip
// rate (sample rate = k * 1.023 MHz).
// Recommended config: k=2, m=3, ftype=LIQUID_FIRFILT_RRC, beta=0.5
// (approximation of the rectangular chip shape), threshold~0.3,
// dphi_max from the expected Doppler shift (+/-5 kHz -> ~0.015 rad/sample).
//
#define GPS_L1CA_CODE_LEN 1023
int scframesync_init_gps_l1ca(unsigned int _prn,                     // satellite PRN number (1..32)
                              liquid_float_complex * _preamble);     // output C/A code chips (BPSK), [size: 1023 x 1]

//
// DVB-S2 (ETSI EN 302 307, physical layer framing)
//
// The preamble is the 26-symbol Start-of-Frame (SOF) field of the PLHEADER
// (bits 18D2E82h, pi/2-BPSK modulated); the remaining 64 PLSC symbols and
// the following PL slots are captured as payload.
// Recommended config: k=2, m=7, ftype=LIQUID_FIRFILT_RRC, beta=0.35
// (or 0.25 / 0.20 per transmission roll-off), threshold~0.5,
// payload_sym_len=64 (PLSC) or more.
//
#define DVBS2_SOF_LEN 26
int scframesync_init_dvbs2_sof(liquid_float_complex * _preamble);    // output SOF symbols (pi/2-BPSK), [size: 26 x 1]

#ifdef __cplusplus
}
#endif

#endif // SC_STANDARDS_H
