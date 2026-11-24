#include "../prf.h"
#include "aes.hpp"

void prf_aes_ctr(const uint8_t *input, size_t output_bits, uint8_t *output, size_t start_index)
{
    assert(output_bits % 128 == 0 && "Output bits must be a multiple of 128 for AES-CTR PRF");
    Cipher::Aes<128> aes((unsigned char *)input);

    uint32_t block_count = (output_bits + 127) / 128; // Calculate number of 128-bit blocks needed

    for (uint32_t i = 0; i < block_count; ++i)
    {
        size_t block_index = start_index + i;
        memcpy(output + i * 16, &block_index, sizeof(block_index));
        memset(output + i * 16 + sizeof(block_index), 0, 16 - sizeof(block_index)); // Set counter to i, rest to zero
        aes.decrypt_block(output + i * 16); // Encrypt the counter - happens inplace
    }
}

uint32_t ceil_prf_size_aes_ctr(size_t output_bits)
{
    return ((output_bits + 127) / 128) * 128; // Round up to the nearest multiple of 128 bits
}