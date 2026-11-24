#pragma once

#include <cstdint>
#include "cryptotools.h"
#include "../config.h"

void prf_sha256(const uint8_t *input, size_t output_bits, uint8_t *output, size_t start_index = 0);
void prf_blake3(const uint8_t *input, size_t output_bits, uint8_t *output, size_t start_index = 0);
void prf_aes_ctr(const uint8_t *input, size_t output_bits, uint8_t *output, size_t start_index = 0);

// Computes the PRF based on the configuration provided.
// The input is a block of size `sizeof(block)` and the output is written to `output`.
// The start_index is used to differentiate between multiple calls to the PRF.
// The output size is specified in bits and must be a multiple of the PRF's block size.
// The PRF is selected based on the `cfg.prf` value, which can be "sha256", "blake3", or "aes".
inline void prf(const config &cfg, const uint8_t *input, size_t output_bits, uint8_t *output, size_t start_index = 0)
{
    if (cfg.prf == VAL_PRF_SHA256)
    {
        prf_sha256(input, output_bits, output, start_index);
    }
    else if (cfg.prf == VAL_PRF_BLAKE3)
    {
        prf_blake3(input, output_bits, output, start_index);
    }
    else if (cfg.prf == VAL_PRF_AES_CTR)
    {
        prf_aes_ctr(input, output_bits, output, start_index);
    }
    else
    {
        throw std::runtime_error("Unknown PRF: " + cfg.prf);
    }
}

uint32_t ceil_prf_size_sha256(size_t output_bits);
uint32_t ceil_prf_size_blake3(size_t output_bits);
uint32_t ceil_prf_size_aes_ctr(size_t output_bits);

// Computes the next largest output size supported by the PRF (in bits). 
inline uint32_t ceil_prf_size(const config &cfg, size_t output_bits) {
    if (cfg.prf == VAL_PRF_SHA256) {
        return ceil_prf_size_sha256(output_bits);
    } else if (cfg.prf == VAL_PRF_BLAKE3) {
        return ceil_prf_size_blake3(output_bits);
    } else if (cfg.prf == VAL_PRF_AES_CTR) {
        return ceil_prf_size_aes_ctr(output_bits);
    } else {
        throw std::runtime_error("Unknown PRF: " + cfg.prf);
    }
}