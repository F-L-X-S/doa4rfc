/**
 * @file signal_generator.h
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

#ifndef SIGNAL_GENERATOR_H
#define SIGNAL_GENERATOR_H
#include <complex>
#include <liquid.h>

/**
 * @brief Generate a sequence of samples, containing a repeating pattern of the length `symbol_len`, 
 *        that is repeated 'symbol_repetitions' times. 
 * @param symbol_len Length (Number of samples) of each symbol.
 * @param seq_repition Number of repetitions of the symbols in the sequence.
 * @param x Pointer to the output array where the generated sequence will be stored.
 * @param mod_type Modulation type for the symbols.
 */
void GenerateRepeatingSequence(unsigned int symbol_len, unsigned int symbol_repetitions, std::complex<float>* x, modulation_scheme mod_type);

/**
 * @brief Generate a sequence of samples by repeating a given symbol pattern.
 * 
 * @param symbol Pointer to the symbol pattern (array of complex<float> values).
 * @param symbol_len Number of samples in the symbol pattern.
 * @param symbol_repetitions Number of times the symbol pattern should be repeated.
 * @param x Pointer to the output array where the generated sequence will be stored.
 */
void GenerateRepeatingSequence(std::complex<float>* symbol, unsigned int symbol_len, unsigned int symbol_repetitions, std::complex<float>* x);
  

/**
 * @brief // Insert the short sequence into the long sequence at the specified start position
 * 
 * @param long_sequence Pointer to longer sequence 
 * @param short_sequence Pointer to shorter sequence 
 * @param seq_start Startng-position in the longer sequence where the shorter sequence will be inserted
 * @param seq_len Length of the shorter sequence
 */
void InsertSequence(std::complex<float>* long_sequence, std::complex<float>* short_sequence, unsigned int seq_start, unsigned int seq_len);

/**
 * @brief Add noise to a sequence of complex samples.
 * 
 * @param sequence Pointer to the sequence of complex samples.
 * @param sequence_len Length of the sequence.
 * @param SNRdB Signal-to-noise ratio in decibels.
 */
void AddNoise(std::complex<float>* sequence, unsigned int sequence_len, float SNRdB);

#endif // SIGNAL_GENERATOR_H