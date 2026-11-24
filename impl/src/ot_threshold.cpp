#include "ot_threshold.h"

#include <cryptoTools/Crypto/AES.h>
#include <libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h>
#include <libOTe/TwoChooseOne/Silent/SilentOtExtSender.h>

#include <algorithm>

namespace mip
{
    using oc::BitVector;
    using oc::block;
    using oc::Matrix;

    namespace
    {
        constexpr u64 kChunkOts = u64(1) << 22; // random OTs per silent-OT instance

        inline bool bitOf(const Matrix<std::uint8_t>& A, u64 row, u64 k)
        {
            return (A(row, k >> 3) >> (k & 7)) & 1;
        }

        // low p bits of a random-OT message as an element of Z_{2^p}
        inline u64 low(const block& b, u64 mask) { return b.get<u64>()[0] & mask; }

        // bit z of the M-bit pad derived from key K (correlation-robust fixed-key AES hash)
        struct Pad
        {
            std::array<block, 2> w; // up to 256 bits
            explicit Pad(const block& K)
            {
                w[0] = oc::mAesFixedKey.hashBlock(K ^ block(0, 1));
                w[1] = oc::mAesFixedKey.hashBlock(K ^ block(0, 2));
            }
            bool operator[](u64 z) const
            {
                return (w[z >> 7].get<u64>()[(z >> 6) & 1] >> (z & 63)) & 1;
            }
        };

        void putBits(BitVector& bv, u64& pos, u64 v, u64 p)
        {
            for (u64 i = 0; i < p; ++i)
                bv[pos++] = (v >> i) & 1;
        }
        u64 getBits(const BitVector& bv, u64& pos, u64 p)
        {
            u64 v = 0;
            for (u64 i = 0; i < p; ++i)
                v |= u64(bv[pos++]) << i;
            return v;
        }
    }

    u64 ThresholdShape::pBits() const
    {
        u64 maxDist = dc + (delta + 1) * tau;
        u64 p = 1;
        while ((u64(1) << p) <= maxDist)
            ++p;
        if (p > 8)
            throw std::runtime_error("ot threshold: modulus above 256 not supported");
        return p;
    }

    Proto otThresholdSender(const ThresholdShape& sh, const Matrix<std::uint8_t>& a,
                            BitVector& out, oc::PRNG& prng, coproto::Socket& chl, u64 threads)
    {
        const u64 N = a.rows(), W = sh.width(), p = sh.pBits(), M = u64(1) << p, mask = M - 1;
        const u64 chunk = std::max<u64>(1, kChunkOts / std::max<u64>(W, p));
        out.resize(N);
        std::vector<u64> s(N, 0);

        // ---- distance: additive shares mod M ----
        for (u64 j0 = 0; j0 < N; j0 += chunk)
        {
            u64 nb = std::min(chunk, N - j0), nOts = nb * W;
            std::vector<std::array<block, 2>> msgs(nOts);
            {
                oc::SilentOtExtSender ot;
                ot.configure(nOts, 2, threads);
                co_await ot.silentSend(msgs, prng, chl);
            }
            BitVector e(nOts);
            co_await chl.recv(e);
            BitVector corr(nOts * p);
            u64 pos = 0;
            for (u64 j = 0; j < nb; ++j)
            {
                u64 acc = 0;
                for (u64 k = 0; k < W; ++k)
                {
                    u64 o = j * W + k;
                    u64 w = k < sh.dc ? 1 : sh.delta + 1;
                    bool al = bitOf(a, j0 + j, k);
                    u64 m0 = low(msgs[o][e[o]], mask), m1 = low(msgs[o][!e[o]], mask);
                    // corr = m0 - m1 + w(1 - 2a)  so that R's m_t + t*corr - (S's m0) = w*(a xor t)
                    u64 c = (m0 + M * 4 - m1 + (al ? M * 4 - w : w)) & mask;
                    putBits(corr, pos, c, p);
                    acc += (al ? w : 0) + M * 4 - m0;
                }
                s[j0 + j] = acc & mask;
            }
            co_await chl.send(std::move(corr));
        }

        // ---- comparison: 1-out-of-M OT from p random OTs ----
        BitVector rho(N);
        rho.randomize(prng);
        for (u64 j0 = 0; j0 < N; j0 += chunk)
        {
            u64 nb = std::min(chunk, N - j0), nOts = nb * p;
            std::vector<std::array<block, 2>> msgs(nOts);
            {
                oc::SilentOtExtSender ot;
                ot.configure(nOts, 2, threads);
                co_await ot.silentSend(msgs, prng, chl);
            }
            BitVector e(nOts);
            co_await chl.recv(e);
            BitVector table(nb * M);
            std::vector<Pad> pads;
            pads.reserve(2 * p);
            for (u64 j = 0; j < nb; ++j)
            {
                pads.clear();
                for (u64 i = 0; i < p; ++i)
                    for (u64 bit = 0; bit < 2; ++bit)
                        pads.emplace_back(msgs[j * p + i][bit ^ e[j * p + i]]); // index by R's real bit
                u64 sj = s[j0 + j];
                for (u64 z = 0; z < M; ++z)
                {
                    bool v = (((sj + z) & mask) <= sh.delta) ^ rho[j0 + j];
                    for (u64 i = 0; i < p; ++i)
                        v ^= pads[2 * i + ((z >> i) & 1)][z];
                    table[j * M + z] = v;
                }
            }
            co_await chl.send(std::move(table));
        }
        out = rho;
    }

    Proto otThresholdReceiver(const ThresholdShape& sh, const Matrix<std::uint8_t>& b,
                              BitVector& out, oc::PRNG& prng, coproto::Socket& chl, u64 threads)
    {
        const u64 N = b.rows(), W = sh.width(), p = sh.pBits(), M = u64(1) << p, mask = M - 1;
        const u64 chunk = std::max<u64>(1, kChunkOts / std::max<u64>(W, p));
        out.resize(N);
        std::vector<u64> v(N, 0);

        for (u64 j0 = 0; j0 < N; j0 += chunk)
        {
            u64 nb = std::min(chunk, N - j0), nOts = nb * W;
            BitVector c(nOts);
            std::vector<block> msgs(nOts);
            {
                oc::SilentOtExtReceiver ot;
                ot.configure(nOts, 2, threads);
                co_await ot.silentReceive(c, msgs, prng, chl);
            }
            BitVector e(nOts);
            for (u64 j = 0; j < nb; ++j)
                for (u64 k = 0; k < W; ++k)
                    e[j * W + k] = c[j * W + k] ^ bitOf(b, j0 + j, k);
            co_await chl.send(std::move(e));
            BitVector corr(nOts * p);
            co_await chl.recv(corr);
            u64 pos = 0;
            for (u64 j = 0; j < nb; ++j)
            {
                u64 acc = 0;
                for (u64 k = 0; k < W; ++k)
                {
                    u64 o = j * W + k;
                    u64 cv = getBits(corr, pos, p);
                    acc += low(msgs[o], mask) + (bitOf(b, j0 + j, k) ? cv : 0);
                }
                v[j0 + j] = acc & mask;
            }
        }

        for (u64 j0 = 0; j0 < N; j0 += chunk)
        {
            u64 nb = std::min(chunk, N - j0), nOts = nb * p;
            BitVector c(nOts);
            std::vector<block> msgs(nOts);
            {
                oc::SilentOtExtReceiver ot;
                ot.configure(nOts, 2, threads);
                co_await ot.silentReceive(c, msgs, prng, chl);
            }
            BitVector e(nOts);
            for (u64 j = 0; j < nb; ++j)
                for (u64 i = 0; i < p; ++i)
                    e[j * p + i] = c[j * p + i] ^ ((v[j0 + j] >> i) & 1);
            co_await chl.send(std::move(e));
            BitVector table(nb * M);
            co_await chl.recv(table);
            for (u64 j = 0; j < nb; ++j)
            {
                u64 z = v[j0 + j];
                bool bit = table[j * M + z];
                for (u64 i = 0; i < p; ++i)
                    bit ^= Pad(msgs[j * p + i])[z];
                out[j0 + j] = bit;
            }
        }
    }
}
