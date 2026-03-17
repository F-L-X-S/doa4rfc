/**
 * @file doa4rfc.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2026-03-03
 * 
 * 
 */

#include <liquid.h>
#include <complex>
#include <vector>                     


#ifndef DOA4RFC_H
#define DOA4RFC_H

 namespace doa4rfc {

    /**
     * @brief Multidimensional vector types for samples and symbols
     * 
     */
    using Sample_t = std::complex<float>;
    using Samples_1dim_t = std::vector<Sample_t>;
    using Samples_2dim_t = std::vector<Samples_1dim_t>;
    using Samples_3dim_t = std::vector<Samples_2dim_t>;

    using Symbol_t = std::complex<float>;  
    using Symbols_1dim_t = std::vector<Symbol_t>;
    using Symbols_2dim_t = std::vector<Symbols_1dim_t>;
    using Symbols_3dim_t = std::vector<Symbols_2dim_t>;

    /**
     * @brief Block of samples with timestamp
     * 
     */
    struct SampleBlock_t
    {
        Samples_1dim_t samples;                      // Received samples
        uint64_t timestamp;                          // Global Nano-Second-Timestamp
    };

    /**
     * @brief Block of symbols with timestamp
     * 
     */
    struct SymbolBlock_t
    {
        Symbols_1dim_t symbols;                      // Received symbols
        uint64_t timestamp;                          // Global Nano-Second-Timestamp
    };

    /**
     * @brief Sample-Block belonging to one frame 
     * 
     */
    struct FrameSamps_t: public SampleBlock_t {
        unsigned int channel;                               // Channel index
    };

    /**
     * @brief Symbol-Block belonging to one frame 
     * 
     */
    struct FrameSyms_t: public SymbolBlock_t {
        unsigned int channel;                               // Channel index
    };

    /**
     * @brief Phase correction structure to store phase adjustments for NCOs
     * 
     */
    struct Phase_t {
        float phi;               // Phase data
        unsigned int channel;    // Channel index
    };
 }

namespace liquid_conv {

   /**
    * @brief Type conversion function to convert the value of a std::complex<float> to liquid_float_complex.
    * 
    * @param p 
    * @return liquid_float_complex
    */
   inline liquid_float_complex Val(const std::complex<float>& c) {
   return {c.real(), c.imag()};
   }

   /**
    * @brief Type conversion function to convert a pointer to std::complex<float> to a pointer to liquid_float_complex.
    * 
    * @param p 
    * @return liquid_float_complex* 
    */
   inline liquid_float_complex* Ptr(std::complex<float>* p) {
      return reinterpret_cast<liquid_float_complex*>(p);
   }

   /**
    * @brief Type conversion function to convert a const pointer to std::complex<float> to a const pointer to liquid_float_complex.
    * 
    * @param p 
    * @return liquid_float_complex* 
    */
   inline const liquid_float_complex* Ptr(const std::complex<float>* p) {
   return reinterpret_cast<const liquid_float_complex*>(p);
   }

   /**
    * @brief Type conversion function to reinterpret a reference to std::complex<float> as a pointer to liquid_float_complex.
    * 
    * @param c 
    * @return liquid_float_complex* 
    */
   inline liquid_float_complex* Ref(std::complex<float>& c) {
    return reinterpret_cast<liquid_float_complex*>(&c);
   }  

   /**
    * @brief Type conversion function to reinterpret a reference to std::complex<float> as a pointer to liquid_float_complex.
    * 
    * @param c 
    * @return liquid_float_complex* 
    */
   inline const liquid_float_complex* Ref(const std::complex<float>& c) {
    return reinterpret_cast<const liquid_float_complex*>(&c);
   }  

 }

#endif // DOA4RFC_H