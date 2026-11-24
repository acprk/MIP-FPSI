#pragma once
// OT-based Hamming threshold test on XOR-shared bit vectors (own implementation of the
// technique of SHADE [Bringer et al.] / otFPSI [xDup, S&P'26]):
//   * distance: one random OT per counted bit gives additive shares mod M of
//     sum_k w_k * (a_k XOR b_k), where S holds a, R holds b (R's bits are the OT choices);
//   * comparison: a 1-out-of-M OT built from log2(M) random OTs (Naor-Pinkas style key
//     combination) lets R obtain T[v] = [ (s + v) mod M <= delta ] XOR rho, where S holds s, rho.
// Output: XOR shares (S: rho, R: received bit) of [ weighted distance <= delta ].
// Bits [0, dc) have weight 1, bits [dc, dc+tau) have weight delta+1 (any non-zero tag rejects).
#include "fpsi.h"

#include <coproto/Socket/Socket.h>
#include <cryptoTools/Common/BitVector.h>
#include <cryptoTools/Common/Matrix.h>
#include <cryptoTools/Crypto/PRNG.h>

namespace mip
{
    struct ThresholdShape
    {
        u64 dc = 0, tau = 0, delta = 0;
        u64 pBits() const;   // log2 M
        u64 width() const { return dc + tau; }
    };

    // S side: a is N x ceil((dc+tau)/8) bytes (bit k of row j = a_{j,k}). Returns rho (N bits).
    Proto otThresholdSender(const ThresholdShape& sh, const oc::Matrix<std::uint8_t>& a,
                            oc::BitVector& out, oc::PRNG& prng, coproto::Socket& chl, u64 threads);
    // R side: b with the same layout. Returns the received bits (N bits).
    Proto otThresholdReceiver(const ThresholdShape& sh, const oc::Matrix<std::uint8_t>& b,
                              oc::BitVector& out, oc::PRNG& prng, coproto::Socket& chl, u64 threads);
}
