#pragma once
// Payload sharing via cuckoo hashing + OPPRF, adapted from volePSI RsCpsi (c) 2022 Visa, MIT.
// Changes: fresh random cuckoo seed (the local modified copy hard-codes block(1,1)); no
// key-equality flag (only mValues/mMapping of Sharing are filled); own class names.
#include "volePSI/RsCpsi.h"

namespace mip
{
    using volePSI::Proto;
    using volePSI::Socket;
    using oc::block;
    using u64 = std::uint64_t;

    class HamCpsiSender : public volePSI::details::RsCpsiBase, public oc::TimerAdapter
    {
    public:
        using Sharing = volePSI::RsCpsiSender::Sharing;
        u64 mNumHash = 3; // cuckoo/simple hash functions (3 or 4)
        // Y: programmed keys; values: one row per key. Output: per-bin shares.
        Proto send(oc::span<block> Y, oc::MatrixView<std::uint8_t> values, Sharing& s, Socket& chl);
    };

    class HamCpsiReceiver : public volePSI::details::RsCpsiBase, public oc::TimerAdapter
    {
    public:
        using Sharing = volePSI::RsCpsiReceiver::Sharing;
        u64 mNumHash = 3; // must match the sender
        // X: cuckoo-hashed query keys. s.mMapping[i] = bin of X[i].
        Proto receive(oc::span<block> X, Sharing& s, Socket& chl);
    };
}
