#pragma once
// Hamming-distance fuzzy PSI from the multi-index probing map and a circuit-PSI stack.
// Sender S holds X (m items), receiver R holds Y (n items); R learns
//   I = { x in X : exists y in Y, H(x,y) <= delta }.
// Leakage beyond sizes: B (max receiver bucket load) to S, U (max probe multiplicity) to R.
#include "mip.h"

#include <coproto/Socket/Socket.h>
#include <cryptoTools/Common/Timer.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <macoro/task.h>

namespace mip
{
    using Proto = coproto::task<void>;

    struct FpsiConfig
    {
        u64 d = 128, delta = 4, b = 5, s = 1;
        // If coverQ > 0, use the CoveringLSH cover with coverQ parts instead of the (b,s) block cover.
        u64 coverQ = 0;
        // Ablation: disable certified-coordinate payload compression.
        bool noCompress = false;
        // Ablation: emulate a UniqC-style mapping (no counters; colliding keys dropped).
        bool uniqOnly = false;
        // Ablation: add a conventional circuit-PSI key-equality check (ssp + log2 N zero tag bits)
        // instead of relying on the distance test to reject unprogrammed bins.
        bool keyCheck = false;
        // Experiment only (injection demo): stop right after the load has been released, i.e.
        // the sender's view up to the point where it depends on Y only through the leakage.
        bool leakOnly = false;
        u64 ssp = 40;
        u64 threads = 1;
        block publicSeed = block(0x5eed, 0x1234);
        // Optional public upper bounds on B and U. If set (> 0), the parties pad to them and
        // abort if their own value exceeds the bound: no data-dependent leakage beyond one bit.
        u64 Bbar = 0, Ubar = 0;
        // Leakage-optimal mode: round B and U up to powers of two, so only ceil(log2 B) and
        // ceil(log2 U) are revealed (at most log2 log2 n bits each), at most 2x the cost.
        bool pow2Leak = false;
        // U-free mode: the sender deduplicates its base probe keys (padded with random dummies to
        // m*Q), the receiver programs n*T keys without copy index, and an oblivious extended
        // permutation maps bin payload shares to the m*Q probe slots. R learns only m.
        bool noU = false;
        // Cuckoo hash functions. Only 3 is supported: cryptoTools' parameter selection (and hence
        // the 2^-ssp failure bound used in the proof) exists only for 2 and 3 hash functions.
        u64 cuckooHashes = 3;
        // Differentially private overestimate of the revealed loads (if dpEps > 0): reveal
        // B + max(0, s + Z), Z two-sided geometric with parameter exp(-dpEps),
        // s = ceil(ln(1/dpDelta)/dpEps); same for U unless noU. Applied before pow2 rounding.
        double dpEps = 0, dpDelta = 9.313225746154785e-10; // 2^-30
        // Switch to the generic all-pairs protocol when it is cheaper (decided from public
        // values and the already revealed B, U only).
        bool allowFallback = true;
        // Threshold test: GMW circuit (default) or OT-based (SHADE-style distance + 1-out-of-M OT;
        // measured no faster here because bin inputs are individually shared, so no OT batching).
        bool gmwThreshold = true;
    };

    struct FpsiInfo
    {
        u64 B = 0, U = 0, Q = 0, T = 0, r = 0, bins = 0, programmed = 0, tau = 0;
        bool fellBack = false;
    };

    // Cost model in AND-gate equivalents used for the fallback decision (public inputs only).
    bool preferAllPairs(u64 m, u64 n, u64 d, u64 N, u64 dc, u64 tau);

    Proto allPairsCoreSend(const FpsiConfig& cfg, const ItemMatrix& X, u64 n, coproto::Socket& chl,
                           oc::PRNG& prng, oc::Timer* timer);
    Proto allPairsCoreRecv(const FpsiConfig& cfg, const ItemMatrix& Y, u64 m, coproto::Socket& chl,
                           oc::PRNG& prng, ItemMatrix& out, oc::Timer* timer);

    Proto fpsiSender(const FpsiConfig& cfg, const ItemMatrix& X, coproto::Socket& chl,
                     oc::PRNG& prng, FpsiInfo& info, oc::Timer* timer = nullptr);

    Proto fpsiReceiver(const FpsiConfig& cfg, const ItemMatrix& Y, coproto::Socket& chl,
                       oc::PRNG& prng, ItemMatrix& out, FpsiInfo& info, oc::Timer* timer = nullptr);

    // Output-matching stage for a pairwise protocol (used to adapt otFPSI): both parties hold XOR
    // shares of the m x n comparison bits (row-major, row i = sender vector x_i); an OR over each row
    // (GMW) and the same OT delivery as our protocol reveal exactly I to the receiver.
    Proto orDeliverSender(const FpsiConfig& cfg, const ItemMatrix& X, const oc::BitVector& share, u64 n,
                          coproto::Socket& chl, oc::PRNG& prng);
    Proto orDeliverReceiver(const FpsiConfig& cfg, u64 m, u64 n, const oc::BitVector& share,
                            coproto::Socket& chl, oc::PRNG& prng, ItemMatrix& out);

    // Baseline: generic assumption-free all-pairs protocol on the same GMW stack
    // (threshold circuit on every (x_i, y_j), OR over j, same delivery step).
    Proto allPairsSender(const FpsiConfig& cfg, const ItemMatrix& X, coproto::Socket& chl,
                         oc::PRNG& prng, oc::Timer* timer = nullptr);
    Proto allPairsReceiver(const FpsiConfig& cfg, const ItemMatrix& Y, coproto::Socket& chl,
                           oc::PRNG& prng, ItemMatrix& out, oc::Timer* timer = nullptr);
}
