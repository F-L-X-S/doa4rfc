/**
 * @file ui_worker.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2025-12-27
 * 
 * 
 */

#ifndef UI_WORKER_H
#define UI_WORKER_H
        
#include <vector>                     
#include <complex>      

#include <queue>       
#include <mutex>                    
#include <condition_variable>         
#include <string>              
#include <atomic>    

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <iostream>

#include <sync_worker/sync_worker.h>

/**
 * @brief terminal_worker reads terminal inputs. 
 * Possible commands:
 * adjust_phase <channel> <phase in rad> : push phase correction for the specified channel to the phi_error_queue 
 * exit : terminate program
 * q : terminate program
 * quit : terminate program
 * 
 * @param phi_error_queue Reference to the thread-safe queue to send phase corrections for channel synchronizers
 * @param stop_signal_called Stop signal to terminate the program
 */
void terminal_worker(    PhaseQueue_t& phi_error_queue,
                        std::atomic<bool>& stop_signal_called);


#endif // UI_WORKER_H