/*
 * Copyright (c) 2007 - 2024 Joseph Gaeddert
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// generic single-carrier frame synchronizer (derived from liquid-dsp's flexframesync)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <complex.h>

#include <liquid.h>
#include "scframesync.h"

// push samples through detection stage
int scframesync_execute_seekpn(scframesync   _q,
                               float complex _x);

// step receiver mixer, matched filter, decimator
//  _q      :   frame synchronizer
//  _x      :   input sample
//  _y      :   output symbol
int scframesync_step(scframesync     _q,
                     float complex   _x,
                     float complex * _y);

// push samples through synchronizer, saving received preamble symbols
int scframesync_execute_rxpreamble(scframesync   _q,
                                   float complex _x);

// receive payload symbols
int scframesync_execute_rxpayload(scframesync   _q,
                                  float complex _x);

// scframesync object structure
struct scframesync_s {
    // callback
    framesync_callback  callback;       // user-defined callback function
    void *              userdata;       // user-defined data structure
    framesyncstats_s    framesyncstats; // frame statistic object (synchronizer)

    // synchronizer parameters (copied from scframesync_config_t)
    unsigned int    preamble_len;       // number of preamble symbols
    unsigned int    k;                  // samples per symbol
    unsigned int    m;                  // pulse-shape filter delay (symbols)
    float           beta;               // pulse-shape excess bandwidth factor
    unsigned int    payload_sym_len;    // number of payload symbols to capture

    // synchronizer objects
    qdetector_cccf  detector;           // pre-demod detector
    float           tau_hat;            // fractional timing offset estimate
    float           dphi_hat;           // carrier frequency offset estimate
    float           phi_hat;            // carrier phase offset estimate
    float           gamma_hat;          // channel gain estimate
    nco_crcf        mixer;              // carrier frequency recovery (coarse)
    unsigned int    frame_len;          // number of samples in frame

    // timing recovery objects, states
    firpfb_crcf     mf;                 // matched filter decimator
    unsigned int    npfb;               // number of filters in symsync
    int             mf_counter;         // matched filter output timer
    unsigned int    pfb_index;          // filterbank index

    // preamble
    float complex * preamble_pn;        // known preamble symbol sequence
    float complex * preamble_rx;        // received preamble symbols

    // payload
    float complex * payload_sym;        // payload symbols (received)

    // status variables
    unsigned int    preamble_counter;   // counter: num of preamble syms received
    unsigned int    symbol_counter;     // counter: num of payload symbols received
    enum {
        SCFRAMESYNC_STATE_DETECTFRAME=0,    // detect frame (seek preamble)
        SCFRAMESYNC_STATE_RXPREAMBLE,       // receive preamble sequence
        SCFRAMESYNC_STATE_RXPAYLOAD,        // receive payload symbols
    }               state;                  // receiver state
};

// create scframesync object
//  _config         :   standard-dependent synchronizer configuration (copied)
//  _callback       :   callback function invoked when frame is received
//  _userdata       :   user-defined data object passed to callback
scframesync scframesync_create(const scframesync_config_t * _config,
                               framesync_callback _callback,
                               void *             _userdata)
{
    // validate input
    if (_config == NULL)
        return liquid_error_config("scframesync_create(), configuration is NULL");
    if (_config->preamble == NULL)
        return liquid_error_config("scframesync_create(), preamble sequence is required");
    if (_config->preamble_len == 0)
        return liquid_error_config("scframesync_create(), preamble length must be greater than zero");
    if (_config->k < 2)
        return liquid_error_config("scframesync_create(), samples per symbol must be at least 2");
    if (_config->m == 0)
        return liquid_error_config("scframesync_create(), filter delay must be greater than zero");
    if (_config->beta <= 0.0f || _config->beta > 1.0f)
        return liquid_error_config("scframesync_create(), excess bandwidth factor must be in (0,1]");
    if (_config->threshold <= 0.0f || _config->threshold >= 2.0f)
        return liquid_error_config("scframesync_create(), detection threshold must be in (0,2)");
    if (_config->dphi_max < 0.0f || _config->dphi_max > 0.5f)
        return liquid_error_config("scframesync_create(), carrier offset search range must be in [0,0.5]");
    if (_config->payload_sym_len == 0)
        return liquid_error_config("scframesync_create(), payload symbol length must be greater than zero");

    scframesync q = (scframesync) malloc(sizeof(struct scframesync_s));
    q->callback = _callback;
    q->userdata = _userdata;

    // copy scalar configuration
    q->preamble_len    = _config->preamble_len;
    q->k               = _config->k;
    q->m               = _config->m;
    q->beta            = _config->beta;
    q->payload_sym_len = _config->payload_sym_len;

    // copy preamble symbol sequence, allocate receive buffer
    q->preamble_pn = (float complex*) malloc(q->preamble_len*sizeof(float complex));
    q->preamble_rx = (float complex*) malloc(q->preamble_len*sizeof(float complex));
    memmove(q->preamble_pn, _config->preamble, q->preamble_len*sizeof(float complex));

    // create frame detector on the pulse-shaped preamble
    q->detector = qdetector_cccf_create_linear(q->preamble_pn, q->preamble_len,
                                               _config->ftype, q->k, q->m, q->beta);
    if (q->detector == NULL) {
        free(q->preamble_pn);
        free(q->preamble_rx);
        free(q);
        return liquid_error_config("scframesync_create(), could not create detector");
    }
    qdetector_cccf_set_threshold(q->detector, _config->threshold);
    if (_config->dphi_max > 0.0f)
        qdetector_cccf_set_range(q->detector, _config->dphi_max);

    // create symbol timing recovery filters
    q->npfb = 32;   // number of filters in the bank
    q->mf   = firpfb_crcf_create_rnyquist(_config->ftype, q->npfb, q->k, q->m, q->beta);

    // create down-converter for carrier frequency/phase recovery (coarse)
    q->mixer = nco_crcf_create(LIQUID_NCO);

    // allocate memory for payload symbols
    q->payload_sym = (float complex*) malloc(q->payload_sym_len*sizeof(float complex));

    // reset state and return
    scframesync_reset(q);
    return q;
}

// destroy frame synchronizer object, freeing all internal memory
int scframesync_destroy(scframesync _q)
{
    // destroy synchronization objects
    qdetector_cccf_destroy(_q->detector);   // frame detector
    firpfb_crcf_destroy   (_q->mf);         // matched filter
    nco_crcf_destroy      (_q->mixer);      // coarse NCO

    // free buffers and arrays
    free(_q->preamble_pn);      // known preamble symbol sequence
    free(_q->preamble_rx);      // received preamble symbols
    free(_q->payload_sym);      // received payload symbols

    // free main object memory
    free(_q);
    return LIQUID_OK;
}

// print frame synchronizer object internals
int scframesync_print(scframesync _q)
{
    printf("scframesync:\n");
    printf("    preamble length     :   %u symbols\n", _q->preamble_len);
    printf("    samples per symbol  :   %u\n", _q->k);
    printf("    filter delay        :   %u symbols\n", _q->m);
    printf("    excess bandwidth    :   %-8.3f\n", _q->beta);
    printf("    payload length      :   %u symbols\n", _q->payload_sym_len);
    return LIQUID_OK;
}

// reset frame synchronizer object
int scframesync_reset(scframesync _q)
{
    // reset binary pre-demod synchronizer
    qdetector_cccf_reset(_q->detector);

    // reset carrier recovery object
    nco_crcf_reset(_q->mixer);

    // reset symbol timing recovery state
    firpfb_crcf_reset(_q->mf);

    // reset state and counters
    _q->state            = SCFRAMESYNC_STATE_DETECTFRAME;
    _q->preamble_counter = 0;
    _q->symbol_counter   = 0;
    _q->frame_len        = 0;

    return LIQUID_OK;
}

// check if frame is open (detection has fired, frame not yet complete)
int scframesync_is_frame_open(scframesync _q)
{
    return (_q->state == SCFRAMESYNC_STATE_DETECTFRAME) ? 0 : 1;
}

// execute frame synchronizer
//  _q  :   frame synchronizer object
//  _x  :   input sample array [size: _n x 1]
//  _n  :   number of input samples
int scframesync_execute(scframesync            _q,
                        liquid_float_complex * _x,
                        unsigned int           _n)
{
    float complex * x = (float complex*) _x;
    unsigned int i;
    for (i=0; i<_n; i++) {
        switch (_q->state) {
        case SCFRAMESYNC_STATE_DETECTFRAME:
            // detect frame (look for preamble sequence)
            scframesync_execute_seekpn(_q, x[i]);
            break;
        case SCFRAMESYNC_STATE_RXPREAMBLE:
            // receive preamble sequence symbols
            scframesync_execute_rxpreamble(_q, x[i]);
            break;
        case SCFRAMESYNC_STATE_RXPAYLOAD:
            // receive payload symbols
            scframesync_execute_rxpayload(_q, x[i]);
            break;
        default:
            return liquid_error(LIQUID_EINT,"scframesync_execute(), unknown/unsupported internal state");
        }

        // Increment frame-length counter
        if (_q->state == SCFRAMESYNC_STATE_DETECTFRAME){
            _q->frame_len = 0;
        } else{
            _q->frame_len ++;
        }
    }

    return LIQUID_OK;
}

//
// internal methods
//

// execute synchronizer, seeking preamble sequence
//  _q      :   frame synchronizer object
//  _x      :   input sample
int scframesync_execute_seekpn(scframesync   _q,
                               float complex _x)
{
    // push through pre-demod synchronizer
    float complex * v = qdetector_cccf_execute(_q->detector, _x);

    // check if frame has been detected
    if (v == NULL)
        return LIQUID_OK;

    // get estimates
    _q->tau_hat   = qdetector_cccf_get_tau  (_q->detector);
    _q->gamma_hat = qdetector_cccf_get_gamma(_q->detector);
    _q->dphi_hat  = qdetector_cccf_get_dphi (_q->detector);
    _q->phi_hat   = qdetector_cccf_get_phi  (_q->detector);

    // set appropriate filterbank index
    if (_q->tau_hat > 0) {
        _q->pfb_index = (unsigned int)(      _q->tau_hat  * _q->npfb) % _q->npfb;
        _q->mf_counter = 0;
    } else {
        _q->pfb_index = (unsigned int)((1.0f+_q->tau_hat) * _q->npfb) % _q->npfb;
        _q->mf_counter = 1;
    }

    // output filter scale (gain estimate, scaled by 1/k samples/symbol)
    firpfb_crcf_set_scale(_q->mf, 1.0f / ((float)(_q->k) * _q->gamma_hat));

    // set frequency/phase of mixer
    nco_crcf_set_frequency(_q->mixer, _q->dphi_hat);
    nco_crcf_set_phase    (_q->mixer, _q->phi_hat );

    // update state
    _q->state = SCFRAMESYNC_STATE_RXPREAMBLE;

    // run buffered samples through synchronizer
    unsigned int buf_len = qdetector_cccf_get_buf_len(_q->detector);
    scframesync_execute(_q, (liquid_float_complex*)v, buf_len);
    return LIQUID_OK;
}

// step receiver mixer, matched filter, decimator
//  _q      :   frame synchronizer
//  _x      :   input sample
//  _y      :   output symbol
int scframesync_step(scframesync     _q,
                     float complex   _x,
                     float complex * _y)
{
    // mix sample down
    float complex v;
    nco_crcf_mix_down(_q->mixer, _x, &v);
    nco_crcf_step    (_q->mixer);

    // push sample into filterbank
    firpfb_crcf_push   (_q->mf, v);
    firpfb_crcf_execute(_q->mf, _q->pfb_index, &v);

    // increment counter to determine if sample is available
    _q->mf_counter++;
    int sample_available = (_q->mf_counter >= 1) ? 1 : 0;

    // set output sample if available
    if (sample_available) {
        // set output
        *_y = v;

        // decrement counter by k samples/symbol
        _q->mf_counter -= (int)_q->k;
    }

    // return flag
    return sample_available;
}

// execute synchronizer, receiving preamble sequence
//  _q      :   frame synchronizer object
//  _x      :   input sample
int scframesync_execute_rxpreamble(scframesync   _q,
                                   float complex _x)
{
    // step synchronizer
    float complex mf_out = 0.0f;
    int sample_available = scframesync_step(_q, _x, &mf_out);

    // compute output if timeout
    if (sample_available) {

        // save output in preamble symbols buffer
        unsigned int delay = 2*_q->m;     // delay from matched filter
        if (_q->preamble_counter >= delay) {
            unsigned int index = _q->preamble_counter-delay;

            _q->preamble_rx[index] = mf_out;
        }

        // update preamble counter
        _q->preamble_counter++;

        // update state
        if (_q->preamble_counter == _q->preamble_len + delay)
            _q->state = SCFRAMESYNC_STATE_RXPAYLOAD;
    }
    return LIQUID_OK;
}

// execute synchronizer, receiving payload
//  _q      :   frame synchronizer object
//  _x      :   input sample
int scframesync_execute_rxpayload(scframesync   _q,
                                  float complex _x)
{
    // step synchronizer
    float complex mf_out = 0.0f;
    int sample_available = scframesync_step(_q, _x, &mf_out);

    // compute output if timeout
    if (sample_available) {
        // save payload symbols
        _q->payload_sym[_q->symbol_counter] = mf_out;

        // increment counter
        _q->symbol_counter++;

        if (_q->symbol_counter == _q->payload_sym_len) {
            // invoke callback
            if (_q->callback != NULL) {
                // set framestats internals
                framesyncstats_init_default(&_q->framesyncstats);
                _q->framesyncstats.rssi          = 20*log10f(_q->gamma_hat);
                _q->framesyncstats.cfo           = nco_crcf_get_frequency(_q->mixer);
                _q->framesyncstats.framesyms     = (liquid_float_complex*)_q->payload_sym;
                _q->framesyncstats.num_framesyms = _q->payload_sym_len;

                // invoke callback method
                _q->callback(NULL,      // header
                             1,         // header valid
                             NULL,      // payload
                             0,         // payload length
                             1,         // payload valid
                             _q->framesyncstats,
                             _q->userdata);
            }

            // reset frame synchronizer
            return scframesync_reset(_q);
        }
    }
    return LIQUID_OK;
}

//
// query methods
//

// received signal strength indication
float scframesync_get_rssi(scframesync _q)
{
    return 20.0f*log10f(_q->gamma_hat);
}

// carrier offset estimate
float scframesync_get_cfo(scframesync _q)
{
    return nco_crcf_get_frequency(_q->mixer);
}

// Return length of detected frame (received samples since detection) in samples
unsigned int scframesync_get_frame_len(scframesync _q)
{
    return _q->frame_len;
}

// Copy the detected frame symbol at continuous position _pos to the specified buffer _x
// Indexing order: preamble (preamble_len symbols) -> payload (payload_sym_len symbols)
unsigned int scframesync_get_sym(scframesync            _q,
                                 liquid_float_complex * _x,
                                 unsigned int           _pos)
{
    float complex * x = (float complex*) _x;

    // preamble symbols
    unsigned int offset = 0;
    if (_pos < offset + _q->preamble_len) {
        *x = _q->preamble_rx[_pos - offset];
        return 1;
    }
    offset += _q->preamble_len;

    // payload symbols
    if (_pos < offset + _q->payload_sym_len) {
        *x = _q->payload_sym[_pos - offset];
        return 1;
    }

    return 0;
}
