/**
 * @file matlab_if.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2025-07-11
 * 
 * 
 */

#ifndef MATLAB_IF_H
#define MATLAB_IF_H

#include <vector>                     
#include <complex>      

#include <queue>       
#include <mutex>                    
#include <condition_variable>         
#include <string>              
#include <atomic>    

#include <iostream>

#include <multithread_worker.h>
#include <grouping_worker.h>
#include <matlabXport.hpp>
#include <doa4rfc.h>

using namespace doa4rfc;
using namespace grouping_worker_queues;


class MatlabWorker: public MultithreadWorker {
    public:
        MatlabWorker(   MatlabXport&                m_xport,
                        std::atomic<bool>&          stop_signal_ref);

        ~MatlabWorker();

        /**
         * @brief Stop thread Execute function and add plotting commands to the MatlabXport instance.
         *
         */
        void StopWorker();

        /**
         * @brief Get the reference to the internal input queue for multi-channel frame samples (timestamped sample vector for each channel belonging to one frame).
         * Dimensions: [channel, sample_index]
         * 
         * @return ThreadSafeQueue<samples_2dim_t>*  Reference to the internal multi-channel frame samples  queue
         */
        ThreadSafeQueue<Samples_2dim_t>* GetMultiChSampsQueue();

        /**
         * @brief Get the reference to the internal input queue for multi-channel frame symbols (timestamped symbol vector for each channel belonging to one frame)
         * Dimensions: [channel, symbol_index]
         * 
         * @return ThreadSafeQueue<Symbols_2dim_t>*  Reference to the internal multi-channel frame symbols  queue
         */
        ThreadSafeQueue<Symbols_2dim_t>* GetMultiChSymsQueue();

        /**
         * @brief Enable or disable continuous MATLAB export
         *
         * @param enabled true to enable, false to disable
         */
        void SetExportEnabled(bool enabled);

        /**
         * @brief Check if continuous MATLAB export is enabled
         *
         * @return true if enabled
         */
        bool GetExportEnabled() const;

        /**
         * @brief Export only the next single frame of each type, then stop exporting
         */
        void ExportSingle();

    protected:

        /**
         * @brief 
         * 
         * This function is executed within a dedicated thread.
         * 
         * @param stop_signal_called Stop signal to terminate the thread
         */
        void Execute() override final;

    private:
        /**
         * @brief Queue to receive multi-channel frame samples (timestamped sample vector for each channel belonging to one frame). 
         * Dimensions: [channel, sample_index]  
         * 
         */
        ThreadSafeQueue<Samples_2dim_t> multich_samps_queue_;

        /**
         * @brief Queue to receive multi-channel frame samples (timestamped symbol vector for each channel belonging to one frame).
         * Dimensions: [channel, sample_index]  
         * 
         */
        ThreadSafeQueue<Symbols_2dim_t> multich_syms_queue_;

        /**
         * @brief Reference to the MatlabXport instance
         */
        MatlabXport& m_xport_;

        /**
         * @brief Counter for the received multi-channel frame sample vectors.
         * 
         */
        unsigned int samps_frame_counter_ = 0;

        /**
         * @brief Counter for the received multi-channel frame symbol vectors.
         * 
         */
        unsigned int syms_frame_counter_ = 0;

        /**
         * @brief Controls whether continuous export is active
         */
        std::atomic<bool> export_enabled_{true};

        /**
         * @brief Remaining single-shot sample frames to export (0 = inactive)
         */
        std::atomic<unsigned int> samps_single_shot_{0};

        /**
         * @brief Remaining single-shot symbol frames to export (0 = inactive)
         */
        std::atomic<unsigned int> syms_single_shot_{0};

        /**
         * @brief Export all available multi-channel frame samples from the multich_samps_queue_ to the MatlabXport instance. 
         * 
         */
        void ExportSampsBuffer(std::vector<Samples_2dim_t> multich_samps_buffer);

        /**
         * @brief Export all available multi-channel frame symbols from the multich_syms_queue_ to the MatlabXport instance. 
         * 
         */
        void ExportSymsBuffer(std::vector<Symbols_2dim_t> multich_syms_buffer);

        /**
         * @brief Add command to plot multi-channel frame samples to the MatlabXport Instance.
         * 
         */
        void AddSampsPlotCommand();

        /**
         * @brief Add command to plot multi-channel frame samples to the MatlabXport Instance.
         * 
         */
        void AddSymsPlotCommand();
};

#endif // MATLAB_IF_H