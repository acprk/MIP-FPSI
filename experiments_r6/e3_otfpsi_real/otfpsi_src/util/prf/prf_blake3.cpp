#include "../prf.h"
#include <blake3.h>
#include <cassert>

// Wrapper for the BLAKE3 reference implementation

void prf_blake3(const uint8_t *input, size_t output_bits, uint8_t *output, size_t start_index) {
    assert(output_bits % 8 == 0 && "Output bits must be a multiple of 8 for BLAKE3 PRF");
    assert(start_index == 0); // Not implemented yet
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, input, sizeof(block));
    blake3_hasher_finalize(&hasher, output, output_bits / 8);
}

uint32_t ceil_prf_size_blake3(size_t output_bits) {
    return ((output_bits + 7) / 8) * 8;
}
