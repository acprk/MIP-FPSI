#pragma once
// Boolean circuits evaluated with volePSI's GMW engine.
#include "volePSI/GMW/Circuit.h"

namespace mip
{
    using u64 = std::uint64_t;

    // Input: bundle 0 = (d + tau) bits: v = x ^ y on the first d bits, tag on the last tau bits.
    // Output: 1 bit = [popcount(v) <= delta] AND [tag == 0].
    // ANDs ~ d (truncated carry-save popcount) + tau.
    volePSI::BetaCircuit thresholdCircuit(u64 d, u64 delta, u64 tau);

    // Input: bundle 0 = q bits. Output: OR of the q bits.
    volePSI::BetaCircuit orCircuit(u64 q);

    // Plaintext self-test of both circuits (throws on failure).
    void circuitSelfTest();
}
