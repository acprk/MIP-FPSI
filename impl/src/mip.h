#pragma once
// Multi-index probing map (MIP) for Hamming-distance fuzzy matching.
//
// d bits are permuted by a public seed and split into b blocks. A table is a set of s blocks;
// all C(b,s) tables are used. If H(x,y) <= delta then some table key differs in at most
//     r(b,s,delta) = s*floor(delta/b) + max(0, s - b + (delta mod b))
// bits (the s smallest of b non-negative block distances summing to <= delta). The sender
// probes the radius-r Hamming ball of each table key; the receiver stores each table key once.
// Bucket collisions are handled by a counter c < B (B = max bucket load, revealed to S), and
// repeated sender probes by a copy index u < U (U = max probe multiplicity, revealed to R).
// No data-distribution assumption is needed; cost scales with B and U.
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Common/Matrix.h>
#include <cstdint>
#include <vector>

namespace mip
{
    using u64 = std::uint64_t;
    using u32 = std::uint32_t;
    using oc::block;

    struct MipParams
    {
        u64 d = 0, delta = 0, b = 0, s = 0, r = 0;
        std::vector<std::vector<u32>> tables; // bit positions of each table key

        static u64 radius(u64 b, u64 s, u64 delta);
        static MipParams make(u64 d, u64 delta, u64 b, u64 s, block publicSeed);
        // CoveringLSH cover (Pagh, SODA'16), partitioned into q parts: in each part, with
        // c = floor(delta/q)+1 random vectors m(i) in {0,1}^c, one table per nonzero v with key
        // positions { i : <m(i),v> = 1 }. Deterministic: any <= delta differences leave some
        // part with <= c-1 of them, whose m(i) are all orthogonal to some nonzero v. Radius 0.
        static MipParams makeCover(u64 d, u64 delta, u64 q, block publicSeed);
        // ablation: verify all d coordinates (no certified-coordinate compression)
        void uncompressed()
        {
            std::vector<u32> all(d);
            for (u32 i = 0; i < d; ++i) all[i] = i;
            for (auto& c : complements) c = all;
            cmax = d;
        }
        // shared tail of both constructors: complements and cmax
        void finalize();

        std::vector<std::vector<u32>> complements; // positions NOT in each table key (sorted)
        u64 cmax = 0;                              // max complement length

        u64 numTables() const { return tables.size(); }
        // pack row bits at positions pos into dst (little-endian bit order), zero padded
        static void gather(const std::uint8_t* row, const std::vector<u32>& pos, std::uint8_t* dst, u64 dstBytes);
        // number of probes per table and per counter value (ball volume), summed over tables
        u64 ballSum() const;
        // probes per sender element for a given B
        u64 probesPerElement(u64 B) const { return ballSum() * B; }
    };

    // Items are rows of a byte matrix (d/8 bytes per row, bit k = byte k/8, bit k%8).
    using ItemMatrix = oc::Matrix<std::uint8_t>;

    struct ReceiverMap
    {
        u64 B = 0;                 // max bucket load
        std::vector<block> base;   // n * T base keys H(t, key, c)
    };
    ReceiverMap receiverMap(const MipParams& p, const ItemMatrix& Y, bool uniqOnly = false);

    struct SenderMap
    {
        u64 Q = 0;                 // probes per element
        u64 U = 0;                 // max multiplicity of a base probe key
        std::vector<block> keys;   // m * Q final keys base ^ u (distinct)
        std::vector<u32> table;    // table index of each probe
        std::vector<std::uint8_t> dist; // H(x_t, z) of each probe (<= r)
    };
    // noCopy: keys are the base keys H(t, z, c) without the copy index (U-free mode; duplicates
    // possible, U is still computed for information only).
    SenderMap senderMap(const MipParams& p, const ItemMatrix& X, u64 B, bool noCopy = false);

    // receiver programmed keys: base ^ u for u < U  (size n*T*U), value row index = j
    void receiverKeys(const ReceiverMap& rm, u64 n, u64 T, u64 U,
                      std::vector<block>& keys, std::vector<u64>& owner);

    // Remove exact duplicate rows (set semantics; does not change the FPSI output).
    ItemMatrix uniqueRows(const ItemMatrix& A);

    inline bool getBit(const std::uint8_t* row, u64 k) { return (row[k >> 3] >> (k & 7)) & 1; }
    u64 hamming(const std::uint8_t* a, const std::uint8_t* b, u64 d);
}
