/**
 * @file sync_worker.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2026-01-06
 * 
 * 
 */

#include "sync_worker/sync_worker.h"

 /**
 * @brief callback function to push received symbols from synchronizer to the cb-data-queue
 * 
 * @param _X array of received subcarrier samples [size: _M x 1]
 * @param _p subcarrier allocation array [size: _M x 1]
 * @param _M number of subcarriers
 * @param _cb_data user-defined data pointer
 * @return return 1 to reset synchronizer after first data symbol
 */
int callback(std::complex<float>* _X, unsigned char * _p, unsigned int _M, void * _cb_data){
    // Add symbols from all subcarriers to buffer 
    for (unsigned int i = 0; i < _M; ++i) {
        // ignore 'null' and 'pilot' subcarriers
        if (_p[i] != OFDMFRAME_SCTYPE_DATA)
            continue;
        static_cast<CallbackData_t*>(_cb_data)->buffer.push_back(_X[i]);  
    }
    // Reset synchronizer after returning the first data symbol (return 1)
    return 1;
}
