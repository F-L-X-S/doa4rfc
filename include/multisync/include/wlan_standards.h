/**
 * @file wlan_standards.h
 * @author Felix Schuelke (flxscode@gmail.com)
 *
 * @brief Standard-specific preset initializers for the wlanframesync
 * configuration (wlanframesync_config_t).
 *
 * Each WLAN standard / channelization is described by its subcarrier
 * allocation, short and long training sequences and pilot base pattern.
 * The helpers in this file fill caller-provided arrays with these
 * characteristics; the caller composes them into a wlanframesync_config_t.
 *
 * To support an additional standard (e.g. IEEE 802.11a/g legacy or 40 MHz
 * channelization), add the corresponding initializer functions here.
 *
 * @version 0.1
 * @date 2026-07-06
 *
 * @copyright Copyright (c) 2026
 */

#ifndef WLAN_STANDARDS_H
#define WLAN_STANDARDS_H

#include <liquid.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// IEEE 802.11n, HT-mixed, 20 MHz channelization (64-pt FFT)
//
int wlanframesync_init_sctype_80211n_20(unsigned int _M,          // IEEE 802.11n HT-Data 20 MHz allocation (52 data, 4 pilots)
                                        unsigned char * _p);
int wlanframesync_init_lstf_80211(unsigned int _M,                // L-STF (IEEE 802.11-2012, Eq. 18-6), stf_period = 16
                                  liquid_float_complex * _stf_seq);
int wlanframesync_init_lltf_80211(unsigned int _M,                // L-LTF (IEEE 802.11-2012, Eq. 18-8), ltf_count = 2
                                  liquid_float_complex * _ltf_seq);
int wlanframesync_init_pilot_base_80211_20(float * _pilot_base);  // 20 MHz pilot pattern {1,1,1,-1} (IEEE 802.11-2012, Eq. 18-22)

#ifdef __cplusplus
}
#endif

#endif // WLAN_STANDARDS_H
