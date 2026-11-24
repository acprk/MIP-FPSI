#include "mip.h"

#include <cryptoTools/Crypto/PRNG.h>
#include <cryptoTools/Crypto/RandomOracle.h>
#include <algorithm>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace mip
{
    namespace
    {
        u64 binom(u64 n, u64 k)
        {
            if (k > n) return 0;
            u64 r = 1;
            for (u64 i = 1; i <= k; ++i)
                r = r * (n - k + i) / i;
            return r;
        }

        struct BlockHash
        {
            size_t operator()(const block& b) const { return b.get<u64>()[0]; }
        };

        // key bits of table t packed little-endian into words
        void extract(const MipParams& p, u64 t, const std::uint8_t* row, std::vector<std::uint8_t>& out)
        {
            auto& pos = p.tables[t];
            out.assign((pos.size() + 7) / 8, 0);
            for (u64 k = 0; k < pos.size(); ++k)
                if (getBit(row, pos[k]))
                    out[k >> 3] |= std::uint8_t(1u << (k & 7));
        }

        block baseKey(u64 t, const std::vector<std::uint8_t>& key, u64 c)
        {
            oc::RandomOracle ro(sizeof(block));
            ro.Update(t);
            ro.Update(c);
            ro.Update(key.data(), key.size());
            block h;
            ro.Final(h);
            return h;
        }

        // enumerate all words within Hamming distance <= r of key (len bits)
        // f(word, number of flipped bits)
        template <typename F>
        void ball(std::vector<std::uint8_t>& key, u64 len, u64 r, u64 start, F&& f, u64 flips = 0)
        {
            f(key, flips);
            if (r == 0)
                return;
            for (u64 k = start; k < len; ++k)
            {
                key[k >> 3] ^= std::uint8_t(1u << (k & 7));
                ball(key, len, r - 1, k + 1, f, flips + 1);
                key[k >> 3] ^= std::uint8_t(1u << (k & 7));
            }
        }
    }

    u64 MipParams::radius(u64 b, u64 s, u64 delta)
    {
        u64 q = delta / b, rem = delta % b;
        u64 extra = (s + rem > b) ? s + rem - b : 0;
        return s * q + extra;
    }

    MipParams MipParams::make(u64 d, u64 delta, u64 b, u64 s, block seed)
    {
        if (b == 0 || s == 0 || s > b || b > d || (s == b && b != 1))
            throw std::runtime_error("bad (b,s)");
        MipParams p;
        p.d = d; p.delta = delta; p.b = b; p.s = s;
        p.r = radius(b, s, delta);

        std::vector<u32> perm(d);
        std::iota(perm.begin(), perm.end(), 0);
        oc::PRNG prng(seed);
        for (u64 i = d - 1; i > 0; --i)
            std::swap(perm[i], perm[prng.get<u64>() % (i + 1)]);
        std::vector<std::vector<u32>> blocks(b);
        for (u64 i = 0; i < d; ++i)
            blocks[i * b / d].push_back(perm[i]);

        std::vector<u64> sel(s);
        std::iota(sel.begin(), sel.end(), 0);
        while (true)
        {
            std::vector<u32> pos;
            for (auto bi : sel)
                pos.insert(pos.end(), blocks[bi].begin(), blocks[bi].end());
            p.tables.push_back(pos);
            // next combination
            u64 i = s;
            while (i > 0 && sel[i - 1] == b - s + i - 1)
                --i;
            if (i == 0)
                break;
            ++sel[i - 1];
            for (u64 j = i; j < s; ++j)
                sel[j] = sel[j - 1] + 1;
        }
        p.finalize();
        return p;
    }

    void MipParams::finalize()
    {
        complements.clear();
        cmax = 0;
        for (auto& t : tables)
        {
            std::vector<char> in(d, 0);
            for (auto v : t) in[v] = 1;
            std::vector<u32> comp;
            for (u32 v = 0; v < d; ++v)
                if (!in[v]) comp.push_back(v);
            cmax = std::max<u64>(cmax, comp.size());
            complements.push_back(std::move(comp));
        }
    }

    MipParams MipParams::makeCover(u64 d, u64 delta, u64 q, block seed)
    {
        if (q == 0 || q > d)
            throw std::runtime_error("bad q");
        MipParams p;
        p.d = d; p.delta = delta; p.b = q; p.s = 0; p.r = 0;
        u64 c = delta / q + 1;
        if (c > 20)
            throw std::runtime_error("cover: too many tables");
        std::vector<u32> perm(d);
        std::iota(perm.begin(), perm.end(), 0);
        oc::PRNG prng(seed);
        for (u64 i = d - 1; i > 0; --i)
            std::swap(perm[i], perm[prng.get<u64>() % (i + 1)]);
        for (u64 part = 0; part < q; ++part)
        {
            std::vector<u32> pos;
            for (u64 i = 0; i < d; ++i)
                if (i * q / d == part) pos.push_back(perm[i]);
            std::vector<u64> mv(pos.size());
            for (auto& m : mv) m = prng.get<u64>() & ((u64(1) << c) - 1);
            for (u64 v = 1; v < (u64(1) << c); ++v)
            {
                std::vector<u32> key;
                for (u64 k = 0; k < pos.size(); ++k)
                    if (__builtin_popcountll(mv[k] & v) & 1)
                        key.push_back(pos[k]);
                std::sort(key.begin(), key.end());
                p.tables.push_back(key);
            }
        }
        p.finalize();
        return p;
    }

    void MipParams::gather(const std::uint8_t* row, const std::vector<u32>& pos, std::uint8_t* dst, u64 dstBytes)
    {
        std::memset(dst, 0, dstBytes);
        for (u64 k = 0; k < pos.size(); ++k)
            if (getBit(row, pos[k]))
                dst[k >> 3] |= std::uint8_t(1u << (k & 7));
    }

    u64 MipParams::ballSum() const
    {
        u64 sum = 0;
        for (auto& t : tables)
            for (u64 e = 0; e <= r; ++e)
                sum += binom(t.size(), e);
        return sum;
    }

    ItemMatrix uniqueRows(const ItemMatrix& A)
    {
        std::vector<u64> idx(A.rows());
        std::iota(idx.begin(), idx.end(), 0);
        auto w = A.cols();
        auto less = [&](u64 a, u64 b) { return std::memcmp(&A(a, 0), &A(b, 0), w) < 0; };
        std::sort(idx.begin(), idx.end(), less);
        std::vector<u64> keep;
        for (u64 k = 0; k < idx.size(); ++k)
            if (k == 0 || std::memcmp(&A(idx[k - 1], 0), &A(idx[k], 0), w) != 0)
                keep.push_back(idx[k]);
        ItemMatrix out(keep.size(), w);
        for (u64 k = 0; k < keep.size(); ++k)
            std::memcpy(&out(k, 0), &A(keep[k], 0), w);
        return out;
    }

    u64 hamming(const std::uint8_t* a, const std::uint8_t* b, u64 d)
    {
        u64 h = 0;
        for (u64 i = 0; i < d / 8; ++i)
            h += __builtin_popcount(unsigned(a[i] ^ b[i]));
        return h;
    }

    ReceiverMap receiverMap(const MipParams& p, const ItemMatrix& Y, bool uniqOnly)
    {
        ReceiverMap rm;
        u64 n = Y.rows(), T = p.numTables();
        rm.base.resize(n * T);
        std::vector<std::uint8_t> key;
        for (u64 t = 0; t < T; ++t)
        {
            std::unordered_map<block, u64, BlockHash> load;
            std::vector<block> bucketOf(n);
            for (u64 j = 0; j < n; ++j)
            {
                extract(p, t, &Y(j, 0), key);
                auto bucket = baseKey(t, key, ~0ull); // bucket id
                bucketOf[j] = bucket;
                auto c = load[bucket]++;
                rm.B = std::max<u64>(rm.B, c + 1);
                rm.base[j * T + t] = baseKey(t, key, c);
            }
            if (uniqOnly)
                // Ablation emulating a UniqC-style mapping: only keys unique in Y are registered;
                // colliding entries get an unreachable key (so matches through them are lost).
                for (u64 j = 0; j < n; ++j)
                    if (load[bucketOf[j]] > 1)
                        rm.base[j * T + t] = baseKey(t, std::vector<std::uint8_t>{1, 2, 3}, (u64(1) << 62) + j);
        }
        if (uniqOnly)
            rm.B = 1;
        return rm;
    }

    SenderMap senderMap(const MipParams& p, const ItemMatrix& X, u64 B, bool noCopy)
    {
        SenderMap sm;
        u64 m = X.rows(), T = p.numTables();
        sm.Q = p.probesPerElement(B);
        sm.keys.resize(m * sm.Q);
        sm.table.resize(m * sm.Q);
        sm.dist.resize(m * sm.Q);
        std::unordered_map<block, u64, BlockHash> mult;
        mult.reserve(m * sm.Q);
        std::vector<std::uint8_t> key;
        for (u64 i = 0; i < m; ++i)
        {
            u64 k = i * sm.Q;
            for (u64 t = 0; t < T; ++t)
            {
                extract(p, t, &X(i, 0), key);
                ball(key, p.tables[t].size(), p.r, 0, [&](const std::vector<std::uint8_t>& z, u64 flips) {
                    for (u64 c = 0; c < B; ++c)
                    {
                        auto bk = baseKey(t, z, c);
                        auto u = mult[bk]++;
                        sm.U = std::max<u64>(sm.U, u + 1);
                        sm.table[k] = u32(t);
                        sm.dist[k] = std::uint8_t(flips);
                        sm.keys[k++] = noCopy ? bk : bk ^ block(0, u);
                    }
                });
            }
            if (k != (i + 1) * sm.Q)
                throw std::runtime_error("probe count mismatch");
        }
        return sm;
    }

    void receiverKeys(const ReceiverMap& rm, u64 n, u64 T, u64 U,
                      std::vector<block>& keys, std::vector<u64>& owner)
    {
        keys.resize(n * T * U);
        owner.resize(n * T * U);
        u64 k = 0;
        for (u64 j = 0; j < n; ++j)
            for (u64 t = 0; t < T; ++t)
                for (u64 u = 0; u < U; ++u)
                {
                    keys[k] = rm.base[j * T + t] ^ block(0, u);
                    owner[k++] = j;
                }
    }
}
