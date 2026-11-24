#pragma once
// Oblivious extended permutation (OEP) over w-byte words, own implementation.
// Functionality (Mohassel–Sadeghian EUROCRYPT'13): the programmer P holds a map
// map: [M] -> [N] (duplicates allowed, M <= N or M > N), the data holder D holds words
// x[0..N); both obtain XOR shares of y with y[j] = x[map[j]].
// Construction: Beneš permutation -> replication layer -> Beneš permutation on
// P' = 2^ceil(log2 max(N, M)) wires. The replication gate is out_j = bit_j ? out_{j-1} : in_j.
// Evaluation as in osn.cpp: D masks every wire with a random w-byte mask, P learns per-gate mask
// corrections by one silent random OT on its control bit (derandomised), D sends both
// corrections encrypted under fixed-key-AES expansions of the OT keys, P evaluates locally.
#include <coproto/Socket/Socket.h>
#include <cryptoTools/Common/Matrix.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <macoro/task.h>
#include <cstdint>
#include <vector>

namespace mip
{
    using u64 = std::uint64_t;
    using u32 = std::uint32_t;

    struct ExtPermNetwork
    {
        // type 0: switch, inputs a,b -> outputs c,d (bit 0: c=a,d=b; bit 1: c=b,d=a)
        // type 1: replication, inputs a (previous output), b (layout input) -> output c
        //         (bit 0: c=b; bit 1: c=a); d unused
        struct Gate { u32 a, b, c, d; std::uint8_t type; };
        u64 n = 0, m = 0, np = 0, wires = 0;
        std::vector<u32> in, out;          // np input wires, m output wires
        std::vector<Gate> gates;           // topological order
        std::vector<std::uint8_t> bit;     // control bits (programmer only)

        // Topology for N inputs and M outputs; if map is non-empty (size M, entries < N), also
        // compute the control bits. The topology depends only on (N, M).
        static ExtPermNetwork build(u64 N, u64 M, const std::vector<u64>& map = {});

        // plaintext evaluation (testing): rows of x are words
        oc::Matrix<std::uint8_t> eval(const oc::Matrix<std::uint8_t>& x) const;
    };

    using Proto = coproto::task<void>;

    // Programmer: map has M entries in [0, N). Output share: M rows of w bytes.
    Proto oepProgrammer(const std::vector<u64>& map, u64 N, u64 w, oc::Matrix<std::uint8_t>& share,
                        oc::PRNG& prng, coproto::Socket& chl, u64 numThreads = 1);

    // Data holder: x has N rows of w bytes. Output share: M rows of w bytes.
    Proto oepHolder(const oc::Matrix<std::uint8_t>& x, u64 M, oc::Matrix<std::uint8_t>& share,
                    oc::PRNG& prng, coproto::Socket& chl, u64 numThreads = 1);
}
