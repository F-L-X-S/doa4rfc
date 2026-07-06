/**
 * @file wlan_standards.c
 * @author Felix Schuelke (flxscode@gmail.com)
 *
 * @brief Standard-specific preset initializers for the wlanframesync
 * configuration; see wlan_standards.h.
 *
 * @version 0.1
 * @date 2026-07-06
 *
 * @copyright Copyright (c) 2026
 */

#include <math.h>
#include <complex.h>

#include <liquid.h>
#include "wlan_standards.h"

// initialize IEEE 802.11n HT-Data subcarrier allocation, 20 MHz (64-pt FFT):
// 52 data, 4 pilots (k=+/-7, +/-21), nulls at DC, k=+/-29..31 and k=-32
int wlanframesync_init_sctype_80211n_20(unsigned int    _M,
                                        unsigned char * _p)
{
    // validate input (allocation defined for the 64-pt FFT at 20 MHz)
    if (_M < 64)
        return liquid_error(LIQUID_EICONFIG,"wlanframesync_init_sctype_80211n_20(), fft size must be at least 64");

    unsigned int i;
    int k;
    for (i=0; i<_M; i++)
        _p[i] = OFDMFRAME_SCTYPE_NULL;
    // positive active band, k=+1..+28
    for (k=1; k<=28; k++)
        _p[k] = OFDMFRAME_SCTYPE_DATA;
    _p[7]  = OFDMFRAME_SCTYPE_PILOT;
    _p[21] = OFDMFRAME_SCTYPE_PILOT;
    // negative active band, k=-28..-1
    for (k=-28; k<=-1; k++)
        _p[_M + k] = OFDMFRAME_SCTYPE_DATA;
    _p[_M - 7]  = OFDMFRAME_SCTYPE_PILOT;
    _p[_M - 21] = OFDMFRAME_SCTYPE_PILOT;
    return LIQUID_OK;
}

// initialize L-STF short training sequence (IEEE 802.11-2012, Eq. 18-6):
// 12 active tones at k = +/-4,8,...,24, time-domain periodic in 16 samples
//  _M        :   total number of subcarriers (64)
//  _stf_seq  :   output sequence (freq), [size: _M x 1]
int wlanframesync_init_lstf_80211(unsigned int           _M,
                                  liquid_float_complex * _stf_seq)
{
    // validate input (sequence defined for the 64-pt FFT at 20 MHz)
    if (_M < 64)
        return liquid_error(LIQUID_EICONFIG,"wlanframesync_init_lstf_80211(), fft size must be at least 64");

    // L-STF tones with values (+/-1 +/-j)/sqrt(2); the standard's sqrt(13/6)
    // power scaling is dropped for unit tone magnitude (the received power is
    // normalized separately in the detection metrics)
    static const int   k_lstf[12] = {-24,-20,-16,-12, -8, -4,  4,  8, 12, 16, 20, 24};
    static const float b_lstf[12] = {  1, -1,  1, -1, -1,  1, -1, -1,  1,  1,  1,  1};

    float complex * S = (float complex*) _stf_seq;
    unsigned int i;
    for (i=0; i<_M; i++)
        S[i] = 0.0f;
    for (i=0; i<12; i++) {
        unsigned int j = (unsigned int)((k_lstf[i] + (int)_M) % (int)_M);
        S[j] = b_lstf[i] * (1.0f + _Complex_I*1.0f) * (float)M_SQRT1_2;
    }
    return LIQUID_OK;
}

// initialize L-LTF long training sequence (IEEE 802.11-2012, Eq. 18-8):
// BPSK on the 52 active tones k = +/-1..26, transmitted twice
//  _M        :   total number of subcarriers (64)
//  _ltf_seq  :   output sequence (freq), [size: _M x 1]
int wlanframesync_init_lltf_80211(unsigned int           _M,
                                  liquid_float_complex * _ltf_seq)
{
    // validate input (sequence defined for the 64-pt FFT at 20 MHz)
    if (_M < 64)
        return liquid_error(LIQUID_EICONFIG,"wlanframesync_init_lltf_80211(), fft size must be at least 64");

    // L-LTF sequence for k = -26..+26 (array index k+26), DC null at k=0
    static const float b_lltf[53] = {
         1, 1,-1,-1, 1, 1,-1, 1,-1, 1, 1, 1, 1, 1, 1,-1,-1, 1, 1,-1, 1,-1, 1, 1, 1, 1,
         0,
         1,-1,-1, 1, 1,-1, 1,-1, 1,-1,-1,-1,-1,-1, 1, 1,-1,-1, 1,-1, 1,-1, 1, 1, 1, 1};

    float complex * S = (float complex*) _ltf_seq;
    unsigned int i;
    int k;
    for (i=0; i<_M; i++)
        S[i] = 0.0f;
    for (k=-26; k<=26; k++) {
        unsigned int j = (unsigned int)((k + (int)_M) % (int)_M);
        S[j] = b_lltf[k+26];
    }
    return LIQUID_OK;
}

// initialize 20 MHz pilot base pattern for subcarriers k = {-21,-7,+7,+21}
// (IEEE 802.11-2012, Eq. 18-22)
//  _pilot_base :   output pattern, [size: 4 x 1]
int wlanframesync_init_pilot_base_80211_20(float * _pilot_base)
{
    _pilot_base[0] =  1.0f;     // k = -21
    _pilot_base[1] =  1.0f;     // k =  -7
    _pilot_base[2] =  1.0f;     // k =  +7
    _pilot_base[3] = -1.0f;     // k = +21
    return LIQUID_OK;
}
