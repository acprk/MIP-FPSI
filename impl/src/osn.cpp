#include "osn.h"

#include <libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h>
#include <libOTe/TwoChooseOne/Silent/SilentOtExtSender.h>

#include <stdexcept>

namespace mip
{
    using oc::BitVector;
    using oc::block;

    namespace
    {
        struct Builder
        {
            BenesNetwork& net;
            bool withBits;

            u32 fresh() { return u32(net.wires++); }

            void addSwitch(u32 a, u32 b, u32 c, u32 d, std::uint8_t s)
            {
                net.sw.push_back({a, b, c, d});
                if (withBits)
                    net.bit.push_back(s);
            }

            // route inputs `inw` so that output k receives input perm[k]; returns output wires
            std::vector<u32> route(const std::vector<u32>& inw, const std::vector<u64>& perm)
            {
                u64 n = inw.size();
                if (n == 1)
                    return inw;
                if (n == 2)
                {
                    u32 c = fresh(), d = fresh();
                    addSwitch(inw[0], inw[1], c, d, withBits ? std::uint8_t(perm[0] == 1) : 0);
                    return {c, d};
                }
                // Arbitrary-size Benes (Waksman) network. h = floor(n/2) input/output switches.
                // Upper subnetwork: h wires; lower subnetwork: n - h wires. If n is odd, the last
                // input enters the lower subnetwork at position h without a switch, and the last
                // output leaves the lower subnetwork at position h without a switch.
                u64 h = n / 2, hl = n - h;
                bool odd = n & 1;
                // side[e]: 0 = element (input index e) travels through the upper subnetwork
                std::vector<std::int8_t> side(n, -1);
                if (withBits)
                {
                    std::vector<u64> inv(n);
                    for (u64 k = 0; k < n; ++k)
                        inv[perm[k]] = k;
                    // input partner of e (or none for the unpaired last input when n is odd)
                    auto inP = [&](u64 e) -> u64 { return (odd && e == n - 1) ? ~u64(0) : (e ^ 1); };
                    // output partner of e (or none if e goes to the unpaired last output)
                    auto outP = [&](u64 e) -> u64 {
                        u64 k = inv[e];
                        return (odd && k == n - 1) ? ~u64(0) : perm[k ^ 1];
                    };
                    // Elements sharing an input switch or an output switch must use different
                    // subnetworks. Constraint graph: paths and even cycles. If n is odd, the two
                    // unpaired elements must use the lower subnetwork; they are the two ends of one
                    // path (or the same element), which has consistent parity.
                    auto colour = [&](u64 e0, std::int8_t c0) {
                        side[e0] = c0;
                        // walk in both directions from e0
                        for (int dir = 0; dir < 2; ++dir)
                        {
                            u64 cur = e0;
                            bool useIn = dir == 0;
                            while (true)
                            {
                                u64 nx = useIn ? inP(cur) : outP(cur);
                                if (nx == ~u64(0) || side[nx] != -1)
                                    break;
                                side[nx] = std::int8_t(1 - side[cur]);
                                cur = nx;
                                useIn = !useIn;
                            }
                        }
                    };
                    if (odd)
                    {
                        colour(n - 1, 1);
                        if (side[perm[n - 1]] == -1)
                            colour(perm[n - 1], 1);
                    }
                    for (u64 e0 = 0; e0 < n; ++e0)
                        if (side[e0] == -1)
                            colour(e0, 0);
                }
                std::vector<u32> up(h), lo(hl);
                std::vector<u64> upPerm(h), loPerm(hl);
                for (u64 i = 0; i < h; ++i)
                {
                    up[i] = fresh();
                    lo[i] = fresh();
                    std::uint8_t s = withBits ? std::uint8_t(side[2 * i] == 1) : 0;
                    addSwitch(inw[2 * i], inw[2 * i + 1], up[i], lo[i], s);
                }
                if (odd)
                    lo[h] = inw[n - 1]; // unswitched last input
                if (withBits)
                {
                    // position of an element inside its subnetwork = index of its input switch
                    // (the unpaired last input sits at lower position h)
                    auto pos = [&](u64 e) -> u64 { return (odd && e == n - 1) ? h : e / 2; };
                    for (u64 j = 0; j < h; ++j)
                    {
                        u64 eU = side[perm[2 * j]] == 0 ? perm[2 * j] : perm[2 * j + 1];
                        u64 eL = side[perm[2 * j]] == 0 ? perm[2 * j + 1] : perm[2 * j];
                        upPerm[j] = pos(eU);
                        loPerm[j] = pos(eL);
                    }
                    if (odd)
                        loPerm[h] = pos(perm[n - 1]);
                }
                auto upOut = route(up, upPerm);
                auto loOut = route(lo, loPerm);
                std::vector<u32> outw(n);
                for (u64 j = 0; j < h; ++j)
                {
                    outw[2 * j] = fresh();
                    outw[2 * j + 1] = fresh();
                    std::uint8_t s = withBits ? std::uint8_t(side[perm[2 * j]] == 1) : 0;
                    addSwitch(upOut[j], loOut[j], outw[2 * j], outw[2 * j + 1], s);
                }
                if (odd)
                    outw[n - 1] = loOut[h]; // unswitched last output
                return outw;
            }
        };

        // 2-bit pad from a random-OT key
        inline std::uint8_t pad2(const block& k) { return k.get<std::uint8_t>()[0] & 3; }
    }

    BenesNetwork BenesNetwork::build(u64 N, const std::vector<u64>& perm)
    {
        BenesNetwork net;
        net.n = N;
        net.np = std::max<u64>(N, 1); // arbitrary-size network: no padding to a power of two
        bool withBits = !perm.empty();
        std::vector<u64> p;
        if (withBits)
        {
            if (perm.size() != N)
                throw std::runtime_error("osn: perm size");
            p = perm;
            std::vector<char> used(net.np, 0);
            for (auto v : perm)
            {
                if (v >= N || used[v])
                    throw std::runtime_error("osn: not a permutation");
                used[v] = 1;
            }
        }
        net.in.resize(net.np);
        for (u64 i = 0; i < net.np; ++i)
            net.in[i] = u32(net.wires++);
        Builder b{net, withBits};
        net.out = b.route(net.in, p);
        return net;
    }

    BitVector BenesNetwork::eval(const BitVector& x) const
    {
        BitVector val(wires);
        for (u64 i = 0; i < n; ++i)
            val[in[i]] = x[i];
        for (u64 k = 0; k < sw.size(); ++k)
        {
            auto& s = sw[k];
            bool va = val[s.a], vb = val[s.b];
            val[s.c] = bit[k] ? vb : va;
            val[s.d] = bit[k] ? va : vb;
        }
        BitVector y(n);
        for (u64 j = 0; j < n; ++j)
            y[j] = val[out[j]];
        return y;
    }

    Proto osnProgrammer(const std::vector<u64>& perm, BitVector& share,
                        oc::PRNG& prng, coproto::Socket& chl, u64 numThreads)
    {
        auto net = BenesNetwork::build(perm.size(), perm);
        u64 S = net.sw.size();

        // random OT: choices c, keys k_c
        BitVector c(S);
        std::vector<block> kc(S);
        if (S)
        {
            oc::SilentOtExtReceiver ot;
            ot.configure(S, 2, numThreads);
            co_await ot.silentReceive(c, kc, prng, chl);
        }
        BitVector e(S);
        for (u64 k = 0; k < S; ++k)
            e[k] = c[k] ^ net.bit[k];
        if (S) co_await chl.send(std::move(e));

        BitVector xm(net.np), z(4 * S);
        co_await chl.recv(xm);
        if (S) co_await chl.recv(z);

        BitVector val(net.wires);
        for (u64 i = 0; i < net.np; ++i)
            val[net.in[i]] = xm[i];
        for (u64 k = 0; k < S; ++k)
        {
            auto& s = net.sw[k];
            u64 off = 4 * k + 2 * c[k];
            std::uint8_t corr = std::uint8_t(z[off] | (z[off + 1] << 1)) ^ pad2(kc[k]);
            bool va = val[s.a], vb = val[s.b];
            bool sb = net.bit[k];
            val[s.c] = (sb ? vb : va) ^ (corr & 1);
            val[s.d] = (sb ? va : vb) ^ ((corr >> 1) & 1);
        }
        share.resize(net.n);
        for (u64 j = 0; j < net.n; ++j)
            share[j] = val[net.out[j]];
    }

    Proto osnHolder(const BitVector& x, BitVector& share,
                    oc::PRNG& prng, coproto::Socket& chl, u64 numThreads)
    {
        auto net = BenesNetwork::build(x.size());
        u64 S = net.sw.size();

        std::vector<std::array<block, 2>> keys(S);
        if (S)
        {
            oc::SilentOtExtSender ot;
            ot.configure(S, 2, numThreads);
            co_await ot.silentSend(keys, prng, chl);
        }
        BitVector e(S);
        if (S) co_await chl.recv(e);

        BitVector r(net.wires);
        r.randomize(prng);
        BitVector xm(net.np);
        for (u64 i = 0; i < net.np; ++i)
            xm[i] = (i < net.n ? x[i] : 0) ^ r[net.in[i]];

        BitVector z(4 * S);
        for (u64 k = 0; k < S; ++k)
        {
            auto& s = net.sw[k];
            std::uint8_t m[2];
            m[0] = std::uint8_t((r[s.a] ^ r[s.c]) | ((r[s.b] ^ r[s.d]) << 1));
            m[1] = std::uint8_t((r[s.b] ^ r[s.c]) | ((r[s.a] ^ r[s.d]) << 1));
            for (u64 b = 0; b < 2; ++b)
            {
                std::uint8_t v = m[b ^ e[k]] ^ pad2(keys[k][b]);
                z[4 * k + 2 * b] = v & 1;
                z[4 * k + 2 * b + 1] = (v >> 1) & 1;
            }
        }
        co_await chl.send(std::move(xm));
        if (S) co_await chl.send(std::move(z));

        share.resize(net.n);
        for (u64 j = 0; j < net.n; ++j)
            share[j] = r[net.out[j]];
    }
}
