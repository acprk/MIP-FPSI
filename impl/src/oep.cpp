#include "oep.h"
#include "osn.h"

#include <cryptoTools/Crypto/AES.h>
#include <libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h>
#include <libOTe/TwoChooseOne/Silent/SilentOtExtSender.h>

#include <cstring>
#include <stdexcept>

namespace mip
{
    using oc::block;
    using oc::BitVector;
    using Mat = oc::Matrix<std::uint8_t>;

    namespace
    {
        u64 nextPow2(u64 v)
        {
            u64 p = 1;
            while (p < v) p <<= 1;
            return p;
        }

        // pad of len bytes from a random-OT key (fixed-key AES correlation-robust hash)
        void expandPad(const block& k, std::uint8_t* dst, u64 len)
        {
            for (u64 i = 0, off = 0; off < len; ++i, off += 16)
            {
                block h = oc::mAesFixedKey.hashBlock(k ^ block(0, i));
                u64 l = std::min<u64>(16, len - off);
                std::memcpy(dst + off, &h, l);
            }
        }

        inline void xorInto(std::uint8_t* dst, const std::uint8_t* a, const std::uint8_t* b, u64 w)
        {
            for (u64 i = 0; i < w; ++i)
                dst[i] = a[i] ^ b[i];
        }

        inline u64 gateBytes(const ExtPermNetwork::Gate& g, u64 w) { return g.type == 0 ? 2 * w : w; }

        // coproto messages carry a 32-bit length: move large buffers in chunks of <= 256 MiB
        constexpr u64 kChunk = u64(1) << 28;

        Proto sendChunked(std::vector<std::uint8_t>& buf, coproto::Socket& chl)
        {
            for (u64 off = 0; off < buf.size(); off += kChunk)
            {
                u64 len = std::min<u64>(kChunk, buf.size() - off);
                std::vector<std::uint8_t> part(buf.begin() + off, buf.begin() + off + len);
                co_await chl.send(std::move(part));
            }
        }

        Proto recvChunked(std::vector<std::uint8_t>& buf, coproto::Socket& chl)
        {
            for (u64 off = 0; off < buf.size(); off += kChunk)
            {
                u64 len = std::min<u64>(kChunk, buf.size() - off);
                std::vector<std::uint8_t> part(len);
                co_await chl.recv(part);
                std::memcpy(buf.data() + off, part.data(), len);
            }
        }
    }

    ExtPermNetwork ExtPermNetwork::build(u64 N, u64 M, const std::vector<u64>& map)
    {
        ExtPermNetwork net;
        net.n = N;
        net.m = M;
        net.np = std::max<u64>({N, M, 1}); // arbitrary-size Benes networks: no power-of-two padding
        u64 P = net.np;
        bool withBits = !map.empty();

        std::vector<u64> perm1, perm2;
        std::vector<std::uint8_t> rep(P, 0);
        if (withBits)
        {
            if (map.size() != M)
                throw std::runtime_error("oep: map size");
            std::vector<u64> cnt(N, 0), start(N, 0), next(N, 0);
            for (auto v : map)
            {
                if (v >= N)
                    throw std::runtime_error("oep: map entry out of range");
                ++cnt[v];
            }
            std::vector<u64> unused;
            for (u64 v = 0; v < P; ++v)
                if (v >= N || cnt[v] == 0)
                    unused.push_back(v);
            perm1.resize(P);
            u64 pos = 0, ui = 0;
            for (u64 v = 0; v < N; ++v)
            {
                if (!cnt[v])
                    continue;
                start[v] = pos;
                perm1[pos++] = v;
                for (u64 k = 1; k < cnt[v]; ++k)
                {
                    rep[pos] = 1;
                    perm1[pos++] = unused[ui++];
                }
            }
            if (pos != M)
                throw std::runtime_error("oep: layout size");
            while (pos < P)
                perm1[pos++] = unused[ui++];
            perm2.resize(P);
            for (u64 s = 0; s < M; ++s)
            {
                u64 v = map[s];
                perm2[s] = start[v] + next[v]++;
            }
            for (u64 s = M; s < P; ++s)
                perm2[s] = s;
        }

        auto b1 = BenesNetwork::build(P, perm1);
        auto b2 = BenesNetwork::build(P, perm2);

        // network 1: wires [0, b1.wires), inputs 0..P-1
        net.wires = b1.wires;
        net.in = b1.in;
        for (u64 k = 0; k < b1.sw.size(); ++k)
        {
            auto& s = b1.sw[k];
            net.gates.push_back({s.a, s.b, s.c, s.d, 0});
            if (withBits) net.bit.push_back(b1.bit[k]);
        }
        // replication layer
        std::vector<u32> R(P);
        R[0] = b1.out[0];
        for (u64 p = 1; p < P; ++p)
        {
            u32 c = u32(net.wires++);
            net.gates.push_back({R[p - 1], b1.out[p], c, 0, 1});
            if (withBits) net.bit.push_back(rep[p]);
            R[p] = c;
        }
        // network 2, relabelled: its input wire i (= i, see BenesNetwork::build) becomes R[i]
        u64 base = net.wires;
        auto rl = [&](u32 w) -> u32 { return w < P ? R[w] : u32(base + (w - P)); };
        net.wires = base + (b2.wires - P);
        for (u64 k = 0; k < b2.sw.size(); ++k)
        {
            auto& s = b2.sw[k];
            net.gates.push_back({rl(s.a), rl(s.b), rl(s.c), rl(s.d), 0});
            if (withBits) net.bit.push_back(b2.bit[k]);
        }
        net.out.resize(M);
        for (u64 j = 0; j < M; ++j)
            net.out[j] = rl(b2.out[j]);
        return net;
    }

    Mat ExtPermNetwork::eval(const Mat& x) const
    {
        u64 w = x.cols();
        Mat val(wires, w);
        for (u64 i = 0; i < n; ++i)
            std::memcpy(&val(in[i], 0), &x(i, 0), w);
        for (u64 k = 0; k < gates.size(); ++k)
        {
            auto& g = gates[k];
            if (g.type == 0)
            {
                std::memcpy(&val(g.c, 0), &val(bit[k] ? g.b : g.a, 0), w);
                std::memcpy(&val(g.d, 0), &val(bit[k] ? g.a : g.b, 0), w);
            }
            else
                std::memcpy(&val(g.c, 0), &val(bit[k] ? g.a : g.b, 0), w);
        }
        Mat y(m, w);
        for (u64 j = 0; j < m; ++j)
            std::memcpy(&y(j, 0), &val(out[j], 0), w);
        return y;
    }

    Proto oepProgrammer(const std::vector<u64>& map, u64 N, u64 w, Mat& share,
                        oc::PRNG& prng, coproto::Socket& chl, u64 numThreads)
    {
        u64 M = map.size();
        auto net = ExtPermNetwork::build(N, M, map);
        u64 S = net.gates.size();

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

        u64 zLen = 0;
        for (auto& g : net.gates)
            zLen += 2 * gateBytes(g, w);
        std::vector<std::uint8_t> xm(net.np * w), z(zLen);
        co_await recvChunked(xm, chl);
        if (zLen) co_await recvChunked(z, chl);

        Mat val(net.wires, w);
        for (u64 i = 0; i < net.np; ++i)
            std::memcpy(&val(net.in[i], 0), &xm[i * w], w);
        std::vector<std::uint8_t> corr(2 * w), pad(2 * w);
        u64 off = 0;
        for (u64 k = 0; k < S; ++k)
        {
            auto& g = net.gates[k];
            u64 L = gateBytes(g, w);
            expandPad(kc[k], pad.data(), L);
            xorInto(corr.data(), &z[off + c[k] * L], pad.data(), L);
            off += 2 * L;
            bool b = net.bit[k];
            if (g.type == 0)
            {
                xorInto(&val(g.c, 0), &val(b ? g.b : g.a, 0), corr.data(), w);
                xorInto(&val(g.d, 0), &val(b ? g.a : g.b, 0), corr.data() + w, w);
            }
            else
                xorInto(&val(g.c, 0), &val(b ? g.a : g.b, 0), corr.data(), w);
        }
        share.resize(M, w);
        for (u64 j = 0; j < M; ++j)
            std::memcpy(&share(j, 0), &val(net.out[j], 0), w);
    }

    Proto oepHolder(const Mat& x, u64 M, Mat& share, oc::PRNG& prng, coproto::Socket& chl, u64 numThreads)
    {
        u64 N = x.rows(), w = x.cols();
        auto net = ExtPermNetwork::build(N, M);
        u64 S = net.gates.size();

        std::vector<std::array<block, 2>> keys(S);
        if (S)
        {
            oc::SilentOtExtSender ot;
            ot.configure(S, 2, numThreads);
            co_await ot.silentSend(keys, prng, chl);
        }
        BitVector e(S);
        if (S) co_await chl.recv(e);

        Mat r(net.wires, w);
        prng.get(r.data(), r.size());
        std::vector<std::uint8_t> xm(net.np * w, 0);
        for (u64 i = 0; i < net.np; ++i)
        {
            const std::uint8_t* ri = &r(net.in[i], 0);
            for (u64 t = 0; t < w; ++t)
                xm[i * w + t] = (i < N ? x(i, t) : 0) ^ ri[t];
        }

        u64 zLen = 0;
        for (auto& g : net.gates)
            zLen += 2 * gateBytes(g, w);
        std::vector<std::uint8_t> z(zLen), msg[2], pad(2 * w);
        msg[0].resize(2 * w);
        msg[1].resize(2 * w);
        u64 off = 0;
        for (u64 k = 0; k < S; ++k)
        {
            auto& g = net.gates[k];
            u64 L = gateBytes(g, w);
            const std::uint8_t *ra = &r(g.a, 0), *rb = &r(g.b, 0), *rc = &r(g.c, 0);
            if (g.type == 0)
            {
                const std::uint8_t* rd = &r(g.d, 0);
                xorInto(msg[0].data(), ra, rc, w);          // bit 0: c = a
                xorInto(msg[0].data() + w, rb, rd, w);      //        d = b
                xorInto(msg[1].data(), rb, rc, w);          // bit 1: c = b
                xorInto(msg[1].data() + w, ra, rd, w);      //        d = a
            }
            else
            {
                xorInto(msg[0].data(), rb, rc, w);          // bit 0: c = b
                xorInto(msg[1].data(), ra, rc, w);          // bit 1: c = a
            }
            for (u64 b = 0; b < 2; ++b)
            {
                expandPad(keys[k][b], pad.data(), L);
                xorInto(&z[off + b * L], msg[b ^ e[k]].data(), pad.data(), L);
            }
            off += 2 * L;
        }
        co_await sendChunked(xm, chl);
        if (zLen) co_await sendChunked(z, chl);

        share.resize(M, w);
        for (u64 j = 0; j < M; ++j)
            std::memcpy(&share(j, 0), &r(net.out[j], 0), w);
    }
}
