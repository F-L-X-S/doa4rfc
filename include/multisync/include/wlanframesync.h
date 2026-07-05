
//
// OFDM frame (symbol) synchronizer
//
typedef int (*ofdmframesync_callback)(liquid_float_complex * _y,
                                      unsigned char * _p,
                                      unsigned int _M,
                                      void * _userdata);
typedef struct ofdmframesync_s * ofdmframesync;

// create OFDM framing synchronizer object
//  _M          :   number of subcarriers, >10 typical
//  _cp_len     :   cyclic prefix length
//  _taper_len  :   taper length (OFDM symbol overlap)
//  _p          :   subcarrier allocation (null, pilot, data), [size: _M x 1]
//  _callback   :   user-defined callback function
//  _userdata   :   user-defined data pointer
ofdmframesync ofdmframesync_create(unsigned int           _M,
                                   unsigned int           _cp_len,
                                   unsigned int           _taper_len,
                                   unsigned char *        _p,
                                   ofdmframesync_callback _callback,
                                   void *                 _userdata);
int ofdmframesync_destroy(ofdmframesync _q);
int ofdmframesync_print(ofdmframesync _q);
int ofdmframesync_reset(ofdmframesync _q);
int ofdmframesync_is_frame_open(ofdmframesync _q);
int ofdmframesync_execute(ofdmframesync _q,
                          liquid_float_complex * _x,
                          unsigned int _n);

// query methods
float ofdmframesync_get_rssi(ofdmframesync _q);     // received signal strength indication
float ofdmframesync_get_cfo(ofdmframesync _q);      // carrier offset estimate
unsigned int ofdmframesync_get_cfr(ofdmframesync _q,        // channel frequency response
                            liquid_float_complex * _x,
                            unsigned int _pos); 
unsigned int ofdmframesync_get_frame_len(ofdmframesync _q); // Get length of detected frame (received symbols since reset) in samples
unsigned int ofdmframesync_get_sym(ofdmframesync _q,        // received data symbol buffer 
                            liquid_float_complex * _x,
                            unsigned int _pos); 
unsigned int ofdmframesync_get_fft_size(ofdmframesync _q);  // size of fft
nco_crcf* ofdmframesync_get_nco(ofdmframesync _q);  // get nco object

// set methods
int ofdmframesync_set_cfo(ofdmframesync _q, float _cfo);  // set carrier offset estimate

// debugging
int ofdmframesync_debug_enable(ofdmframesync _q);
int ofdmframesync_debug_disable(ofdmframesync _q);
int ofdmframesync_debug_print(ofdmframesync _q, const char * _filename);
