/**
 * @file signal_generator.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief This file contains the definition of the SignalGenerator class,
 * which is used to generate a sequence of samples, containing a repeating pattern of the length `symbol_len`,
 * that is repeated 'symbol_repetitions' times.
 * The class provides functions to generate a repeating sequence, insert a sequence into another sequence,
 * and add noise to a sequence of complex samples.
 * Liquid's DSP-modules are based on https://github.com/jgaeddert/liquid-dsp (Copyright (c) 2007 - 2016 Joseph Gaeddert).
 * 
 * @version 0.1
 * @date 2025-05-20
 * 
 * 
 */


#include "signal_generator.h"

#include <cmath>
#include <cstdlib>
#include <cassert>


/**
 * @brief Generate a sequence of samples, containing a repeating pattern of the length `symbol_len`, 
 *        that is repeated 'symbol_repetitions' times. 
 * @param symbol_len Length (Number of samples) of each symbol.
 * @param seq_repition Number of repetitions of the symbols in the sequence.
 * @param x Pointer to the output array where the generated sequence will be stored.
 * @param mod_type Modulation type for the symbols.
 */
void GenerateRepeatingSequence(unsigned int symbol_len, unsigned int symbol_repetitions, std::complex<float>* x, modulation_scheme mod_type) {
    // Create a symbol 
    std::complex<float> symbol[symbol_len];  
    modemcf mod = modemcf_create(mod_type);
    for (unsigned int i=0; i<symbol_len; i++)
        modemcf_modulate(mod, rand()%4, &symbol[i]);
    modemcf_destroy(mod);

    // Insert repeating symbol into output array
    unsigned int t=0;
    for (unsigned int i=0; i<symbol_repetitions; i++) {
        // copy symbol-samples 
        memmove(&x[t], symbol, symbol_len*sizeof(std::complex<float>));
        t += symbol_len;
    }
};

/**
 * @brief Generate a sequence of samples by repeating a given pattern.
 * 
 * @param pattern Pointer to the symbol pattern (array of complex<float> values).
 * @param pattern_len Number of samples in the symbol pattern.
 * @param pattern_repetitions Number of times the pattern should be repeated.
 * @param x Pointer to the output array where the generated sequence will be stored.
 */
void GenerateRepeatingSequence(std::complex<float>* pattern, unsigned int pattern_len, unsigned int symbol_repetitions, std::complex<float>* x) {
    unsigned int t = 0;
    for (unsigned int i = 0; i < symbol_repetitions; i++) {
        memmove(&x[t], pattern, pattern_len * sizeof(std::complex<float>));
        t += pattern_len;
    }
}


/**
 * @brief Insert the short sequence into the long sequence at the specified start position
 * 
 * @param long_sequence Pointer to longer sequence 
 * @param short_sequence Pointer to shorter sequence 
 * @param seq_start Startng-position in the longer sequence where the shorter sequence will be inserted
 * @param seq_len Length of the shorter sequence
 */
void InsertSequence(std::complex<float>* long_sequence, std::complex<float>* short_sequence, unsigned int seq_start, unsigned int seq_len) {
    for (unsigned int i=0; i<seq_len; i++)
        long_sequence[seq_start+i] = short_sequence[i];
};


/**
 * @brief Add noise to a sequence of complex samples.
 * 
 * @param sequence Pointer to the sequence of complex samples.
 * @param sequence_len Length of the sequence.
 * @param SNRdB Signal-to-noise ratio in decibels.
 */
void AddNoise(std::complex<float>* sequence, unsigned int sequence_len, float SNRdB) {
    float nstd = powf(10.0f, -SNRdB/20.0f);
    for (unsigned int i=0; i<sequence_len; i++)
        cawgn(&sequence[i],nstd);
};