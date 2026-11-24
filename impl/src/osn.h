#pragma once
// Oblivious switching network ("permute + share"), own implementation.
// Functionality (Mohassel–Sadeghian EUROCRYPT'13; Garimella et al. PKC'21 §3.2):
//   programmer P holds a permutation perm on [N], data holder D holds bits x[0..N);
//   both obtain XOR shares of y with y[k] = x[perm[k]].
// Construction: arbitrary-size Beneš-style network on N wires (no power-of-two padding since
// build6; build5 and earlier padded to 2^ceil(log N)), routed by the looping
// algorithm; D masks every wire, P learns per-switch mask corrections by 1-out-of-2 OT on its
// switch bit (silent random OT + derandomisation), then evaluates the masked network locally.
#include <coproto/Socket/Socket.h>
#include <cryptoTools/Common/BitVector.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <macoro/task.h>
#include <cstdint>
#include <vector>

namespace mip
{
    using u64 = std::uint64_t;
    using u32 = std::uint32_t;

    struct BenesNetwork
    {
        struct Switch { u32 a, b, c, d; }; // inputs a,b -> outputs c,d; bit 0: c=a,d=b; 1: c=b,d=a
        u64 n = 0, np = 0, wires = 0;
        std::vector<u32> in, out;
        std::vector<Switch> sw;       // topological order
        std::vector<std::uint8_t> bit; // switch bits (programmer only)

        // Topology for N items; if perm is non-empty also compute switch bits for it
        // (perm[k] = index of the input that must arrive at output k).
        static BenesNetwork build(u64 N, const std::vector<u64>& perm = {});

        // plaintext evaluation (testing)
        oc::BitVector eval(const oc::BitVector& x) const;
    };

    using Proto = coproto::task<void>;

    Proto osnProgrammer(const std::vector<u64>& perm, oc::BitVector& share,
                        oc::PRNG& prng, coproto::Socket& chl, u64 numThreads = 1);

    Proto osnHolder(const oc::BitVector& x, oc::BitVector& share,
                    oc::PRNG& prng, coproto::Socket& chl, u64 numThreads = 1);
}
