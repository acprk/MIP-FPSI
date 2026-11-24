#include "../prf.h"
#include "sha256.h"

#pragma GCC diagnostic ignored "-Wunused-parameter" // Suppress unused parameter warnings if SHA is not enabled

// Hashes a 128-bit input block with a 32-bit block index to produce a variable-length output.
void prf_sha256(const uint8_t *input, size_t output_bits, uint8_t *output, size_t start_index)
{
#ifdef ENABLE_SHA
    assert(output_bits % 256 == 0 && "Output bits must be a multiple of 256 for SHA-256 PRF");
    uint8_t buffer[64];
    uint32_t block_count = (output_bits + 255) / 256;

    memcpy(buffer, input, sizeof(block));
    memset(buffer + sizeof(block), 0, sizeof(buffer) - sizeof(block));

    // We hash a 128 bit block + a 32 bit block index. Set padding:
    buffer[20] = 0x80;
    buffer[63] = 160;

    for (uint32_t i = 0; i < block_count; ++i)
    {
        uint8_t *target = output + i * (256 / 8);

        // Initialize the hash state
        memcpy(target, sha256_initial_state, 256 / 8);

        size_t block_index = start_index + i;
        memcpy(buffer + sizeof(block_index), &block_index, sizeof(block_index));
        sha256_process_x86((uint32_t *)target, buffer, sizeof(buffer));
    }
#else
    // If SHA is not enabled, we cannot perform the PRF.
    assert(false && "SHA-256 PRF is not enabled. Please enable it in the build configuration (e.g., using -DWITH_SHA=ON).");
#endif
}

uint32_t ceil_prf_size_sha256(size_t output_bits)
{
    return ((output_bits + 255) / 256) * 256;
}
