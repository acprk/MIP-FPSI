#include "fpsi.h"

#include "HamCpsi.h"
#include "circuits.h"
#include "volePSI/GMW/Gmw.h"
#include "osn.h"
#include "oep.h"
#include "ot_threshold.h"

#include <libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h>
#include <libOTe/TwoChooseOne/Silent/SilentOtExtSender.h>

#include <algorithm>
#include <cmath>
#include "cryptoTools/Common/CuckooIndex.h"
#include <stdexcept>
#include <unordered_map>

namespace mip
{
    using oc::BitVector;
    using oc::Matrix;

    namespace
    {
        void tp(oc::Timer* t, const char* s)
        {
            if (t) t->setTimePoint(s);
        }

        BitVector outputBits(volePSI::Gmw& g, u64 n)
        {
            auto ss = g.getOutputView(0);
            BitVector bv(n);
            std::copy(ss.begin(), ss.begin() + bv.sizeBytes(), bv.data());
            return bv;
        }

        u64 nextPow2(u64 v)
        {
            u64 p = 1;
            while (p < v) p <<= 1;
            return p;
        }

        // One-sided DP noise eta = max(0, s + Z), Z = G1 - G2 with G_i geometric
        // (P(G = g) = (1-a) a^g, a = exp(-eps)), i.e. P(Z = z) ~ exp(-eps |z|);
        // s = ceil(ln(1/delta)/eps). Randomness from the party's PRNG (AES-CTR).
        u64 dpNoise(oc::PRNG& prng, double eps, double delta)
        {
            auto geom = [&]() -> double {
                // uniform in (0, 1]
                double u = (double((prng.get<u64>() >> 11)) + 1.0) / 9007199254740992.0;
                return std::floor(std::log(u) / (-eps));
            };
            double s = std::ceil(std::log(1.0 / delta) / eps);
            double z = geom() - geom();
            double eta = s + z;
            return eta <= 0 ? 0 : u64(eta);
        }

        // Revealed load: public bound, else (DP overestimate, then) optional pow2 rounding.
        u64 revealedLoad(u64 v, u64 bound, const FpsiConfig& cfg, oc::PRNG& prng, const char* what)
        {
            if (bound && cfg.dpEps > 0)
                throw std::runtime_error("public bounds and DP noise are exclusive policies");
            if (bound)
            {
                if (v > bound)
                    throw std::runtime_error(std::string(what) + " exceeds its public bound: abort");
                return bound;
            }
            if (cfg.dpEps > 0)
                v += dpNoise(prng, cfg.dpEps, cfg.dpDelta);
            if (cfg.pow2Leak)
                v = nextPow2(v); // only ceil(log2 v) is revealed
            return v;
        }

        struct BlockHashF
        {
            std::size_t operator()(const oc::block& b) const { return b.get<u64>()[0] ^ (b.get<u64>()[1] * 0x9e3779b97f4a7c15ull); }
        };

        // Tag bits so that a garbage payload passes the threshold test with prob <= 2^-ssp over
        // all N bins: N * Vol(d, delta) / 2^(d + tau) <= 2^-ssp.
        u64 tagBits(u64 d, u64 delta, u64 N, u64 ssp)
        {
            double logVol = 0, vol = 0, c = 1;
            for (u64 e = 0; e <= std::min(delta, d); ++e)
            {
                vol += c;
                c = c * double(d - e) / double(e + 1);
            }
            logVol = std::log2(vol);
            double need = double(ssp) + std::log2(double(std::max<u64>(N, 2))) + logVol - double(d);
            return need <= 0 ? 0 : u64(std::ceil(need));
        }

        // OR-input matrix: row i holds bits share[i*Q .. i*Q+Q)
        Matrix<std::uint8_t> groupBits(const BitVector& share, u64 m, u64 Q)
        {
            Matrix<std::uint8_t> g(m, oc::divCeil(Q, 8));
            for (u64 i = 0; i < m; ++i)
                for (u64 k = 0; k < Q; ++k)
                    if (share[i * Q + k])
                        g(i, k >> 3) |= std::uint8_t(1u << (k & 7));
            return g;
        }

        void padXor(block key, std::uint8_t* dst, const std::uint8_t* src, u64 len)
        {
            oc::PRNG g(key);
            std::vector<std::uint8_t> pad(len);
            g.get(pad.data(), len);
            for (u64 k = 0; k < len; ++k)
                dst[k] = src[k] ^ pad[k];
        }

        ItemMatrix shuffled(const ItemMatrix& A, oc::PRNG& prng)
        {
            u64 m = A.rows(), w = A.cols();
            std::vector<u64> pi(m);
            for (u64 i = 0; i < m; ++i) pi[i] = i;
            for (u64 i = m; i > 1; --i)
                std::swap(pi[i - 1], pi[prng.get<u64>() % i]);
            ItemMatrix X(m, w);
            for (u64 i = 0; i < m; ++i)
                std::memcpy(&X(i, 0), &A(pi[i], 0), w);
            return X;
        }

        // Delivery tag: a dummy decrypts to a zero tag with prob. 2^-(8*bytes); require
        // m * 2^-(8*bytes) <= 2^-ssp.
        u64 deliveryTagBytes(u64 m, u64 ssp)
        {
            return oc::divCeil(ssp + oc::log2ceil(m + 1), 8);
        }

        // Final delivery: chosen OT on fresh keys k_{i,0}, k_{i,1}; R chooses with its share oR[i];
        // the pad under k_{i,c} hides x_i || 0^tag iff oS[i] ^ c = 1, else a random string.
        Proto deliverSend(const FpsiConfig& cfg, const ItemMatrix& X, const BitVector& oS,
                          oc::PRNG& prng, coproto::Socket& chl)
        {
            u64 m = X.rows(), db = cfg.d / 8;
            std::vector<std::array<block, 2>> keys(m);
            prng.get(keys.data(), m);
            {
                oc::SilentOtExtSender ot;
                ot.configure(m, 2, cfg.threads);
                co_await ot.sendChosen(keys, prng, chl);
            }
            u64 len = db + deliveryTagBytes(m, cfg.ssp);
            Matrix<std::uint8_t> ct(2 * m, len);
            std::vector<std::uint8_t> pt(len);
            for (u64 i = 0; i < m; ++i)
                for (u64 c = 0; c < 2; ++c)
                {
                    if ((oS[i] ^ c) == 1)
                    {
                        std::memcpy(pt.data(), &X(i, 0), db);
                        std::fill(pt.begin() + db, pt.end(), 0);
                    }
                    else
                        prng.get(pt.data(), len);
                    padXor(keys[i][c], &ct(2 * i + c, 0), pt.data(), len);
                }
            co_await chl.send(std::move(ct));
        }

        Proto deliverRecv(const FpsiConfig& cfg, u64 m, const BitVector& oR, ItemMatrix& out,
                          oc::PRNG& prng, coproto::Socket& chl)
        {
            u64 db = cfg.d / 8;
            std::vector<block> keysR(m);
            {
                oc::SilentOtExtReceiver ot;
                ot.configure(m, 2, cfg.threads);
                co_await ot.receiveChosen(oR, keysR, prng, chl);
            }
            u64 len = db + deliveryTagBytes(m, cfg.ssp);
            Matrix<std::uint8_t> ct(2 * m, len);
            co_await chl.recv(ct);
            std::vector<std::vector<std::uint8_t>> found;
            std::vector<std::uint8_t> pt(len);
            for (u64 i = 0; i < m; ++i)
            {
                padXor(keysR[i], pt.data(), &ct(2 * i + oR[i], 0), len);
                if (std::all_of(pt.begin() + db, pt.end(), [](std::uint8_t v) { return v == 0; }))
                    found.emplace_back(pt.begin(), pt.begin() + db);
            }
            out.resize(found.size(), db);
            for (u64 i = 0; i < found.size(); ++i)
                std::memcpy(&out(i, 0), found[i].data(), db);
        }
    }

    Proto orDeliverSender(const FpsiConfig& cfg, const ItemMatrix& X, const BitVector& share, u64 n,
                          coproto::Socket& chl, oc::PRNG& prng)
    {
        u64 m = X.rows();
        BitVector oS;
        {
            auto cir = orCircuit(n);
            volePSI::Gmw g;
            g.init(m, cir, cfg.threads, 0, prng.get());
            auto in = groupBits(share, m, n);
            g.setInput(0, oc::MatrixView<std::uint8_t>(in));
            co_await g.run(chl);
            oS = outputBits(g, m);
        }
        co_await deliverSend(cfg, X, oS, prng, chl);
    }

    Proto orDeliverReceiver(const FpsiConfig& cfg, u64 m, u64 n, const BitVector& share,
                            coproto::Socket& chl, oc::PRNG& prng, ItemMatrix& out)
    {
        BitVector oR;
        {
            auto cir = orCircuit(n);
            volePSI::Gmw g;
            g.init(m, cir, cfg.threads, 1, prng.get());
            auto in = groupBits(share, m, n);
            g.setInput(0, oc::MatrixView<std::uint8_t>(in));
            co_await g.run(chl);
            oR = outputBits(g, m);
        }
        co_await deliverRecv(cfg, m, oR, out, prng, chl);
    }

    // ---- U-free mode (cfg.noU) ----
    // S: base probe keys of the m*Q slots, deduplicated and padded with random dummies to m*Q
    // distinct cuckoo items; payload shares per bin; oblivious extended permutation bins -> slots
    // (S programs it, R holds its bin shares); S adds its x-dependent bits per slot; threshold per
    // slot; OR over the Q consecutive slots of each element; delivery. R learns m only.
    static Proto senderNoU(const FpsiConfig& cfg, const MipParams& p, const ItemMatrix& X, u64 n, u64 B,
                           u64 Q, u64 T, u64 N, u64 dc, u64 tau, u64 pb, coproto::Socket& chl,
                           oc::PRNG& prng, FpsiInfo& info, oc::Timer* timer)
    {
        u64 m = X.rows(), M = m * Q;
        auto sm = senderMap(p, X, B, /*noCopy=*/true);
        std::vector<block> items;
        items.reserve(M);
        std::vector<u64> slotItem(M);
        {
            std::unordered_map<block, u64, BlockHashF> idx;
            idx.reserve(M);
            for (u64 s = 0; s < M; ++s)
            {
                auto it = idx.emplace(sm.keys[s], items.size());
                if (it.second)
                    items.push_back(sm.keys[s]);
                slotItem[s] = it.first->second;
            }
            // pad with random dummies (never programmed by R except with prob. ~ n*T*M/2^128)
            while (items.size() < M)
                items.push_back(prng.get<block>());
        }
        info = {B, 1, Q, T, p.r, 0, n * T};
        tp(timer, "S map");

        auto cpsi = std::make_unique<HamCpsiReceiver>();
        HamCpsiReceiver::Sharing rs;
        cpsi->init(n * T, M, pb, cfg.ssp, prng.get(), cfg.threads);
        cpsi->mNumHash = cfg.cuckooHashes;
        co_await cpsi->receive(items, rs, chl);
        if (rs.mValues.rows() != N)
            throw std::runtime_error("bin count mismatch");
        info.bins = N;
        info.tau = tau;
        tp(timer, "S cpsi");

        // extended permutation: slot s <- bin of its distinct item
        Matrix<std::uint8_t> in0;
        {
            std::vector<u64> map(M);
            for (u64 s = 0; s < M; ++s)
                map[s] = rs.mMapping[slotItem[s]];
            co_await oepProgrammer(map, N, pb, in0, prng, chl, cfg.threads);
            std::vector<std::uint8_t> buf(pb);
            for (u64 s = 0; s < M; ++s)
            {
                const std::uint8_t* own = &rs.mValues(map[s], 0);
                std::fill(buf.begin(), buf.end(), 0);
                MipParams::gather(&X(s / Q, 0), p.complements[sm.table[s]], buf.data(), pb);
                for (u64 e = 0; !cfg.noCompress && e < sm.dist[s]; ++e)
                    buf[(p.cmax + e) >> 3] |= std::uint8_t(1u << ((p.cmax + e) & 7));
                for (u64 t = 0; t < pb; ++t)
                    in0(s, t) ^= own[t] ^ buf[t];
            }
        }
        tp(timer, "S extperm");

        BitVector eS;
        {
            auto cir = thresholdCircuit(dc, cfg.delta, tau);
            volePSI::Gmw g;
            g.init(M, cir, cfg.threads, 0, prng.get());
            g.setInput(0, oc::MatrixView<std::uint8_t>(in0));
            co_await g.run(chl);
            eS = outputBits(g, M);
        }
        tp(timer, "S threshold");

        BitVector oS;
        {
            auto cir = orCircuit(Q);
            volePSI::Gmw g;
            g.init(m, cir, cfg.threads, 0, prng.get());
            auto in = groupBits(eS, m, Q);
            g.setInput(0, oc::MatrixView<std::uint8_t>(in));
            co_await g.run(chl);
            oS = outputBits(g, m);
        }
        tp(timer, "S or");
        co_await deliverSend(cfg, X, oS, prng, chl);
        tp(timer, "S deliver");
    }

    static Proto receiverNoU(const FpsiConfig& cfg, const MipParams& p, const ItemMatrix& Y, const ReceiverMap& rm,
                             u64 m, u64 B, u64 Q, u64 T, u64 N, u64 dc, u64 tau, u64 pb,
                             coproto::Socket& chl, oc::PRNG& prng, ItemMatrix& out, FpsiInfo& info,
                             oc::Timer* timer)
    {
        u64 n = Y.rows(), M = m * Q;
        std::vector<block> keys;
        std::vector<u64> owner;
        receiverKeys(rm, n, T, 1, keys, owner);
        Matrix<std::uint8_t> vals(keys.size(), pb);
        for (u64 k = 0; k < keys.size(); ++k)
            MipParams::gather(&Y(owner[k], 0), p.complements[k % T], &vals(k, 0), pb);
        info = {B, 1, Q, T, p.r, N, keys.size(), tau};
        tp(timer, "R map");

        auto cpsi = std::make_unique<HamCpsiSender>();
        HamCpsiSender::Sharing ss;
        cpsi->init(keys.size(), M, pb, cfg.ssp, prng.get(), cfg.threads);
        cpsi->mNumHash = cfg.cuckooHashes;
        co_await cpsi->send(keys, vals, ss, chl);
        if (ss.mValues.rows() != N)
            throw std::runtime_error("bin count mismatch");
        tp(timer, "R cpsi");

        Matrix<std::uint8_t> in1;
        {
            Matrix<std::uint8_t> bins(N, pb);
            std::memcpy(bins.data(), ss.mValues.data(), N * pb);
            co_await oepHolder(bins, M, in1, prng, chl, cfg.threads);
        }
        tp(timer, "R extperm");

        BitVector eR;
        {
            auto cir = thresholdCircuit(dc, cfg.delta, tau);
            volePSI::Gmw g;
            g.init(M, cir, cfg.threads, 1, prng.get());
            g.setInput(0, oc::MatrixView<std::uint8_t>(in1));
            co_await g.run(chl);
            eR = outputBits(g, M);
        }
        tp(timer, "R threshold");

        BitVector oR;
        {
            auto cir = orCircuit(Q);
            volePSI::Gmw g;
            g.init(m, cir, cfg.threads, 1, prng.get());
            auto in = groupBits(eR, m, Q);
            g.setInput(0, oc::MatrixView<std::uint8_t>(in));
            co_await g.run(chl);
            oR = outputBits(g, m);
        }
        tp(timer, "R or");
        co_await deliverRecv(cfg, m, oR, out, prng, chl);
        tp(timer, "R deliver");
    }

    Proto fpsiSender(const FpsiConfig& cfg, const ItemMatrix& Xin, coproto::Socket& chl,
                     oc::PRNG& prng, FpsiInfo& info, oc::Timer* timer)
    {
        auto p = cfg.coverQ ? MipParams::makeCover(cfg.d, cfg.delta, cfg.coverQ, cfg.publicSeed) : MipParams::make(cfg.d, cfg.delta, cfg.b, cfg.s, cfg.publicSeed);
        if (cfg.noCompress) p.uncompressed();
        // 0. local dedup (set semantics) and shuffle so that output order reveals nothing
        auto X0 = uniqueRows(Xin);
        if (cfg.d == 0 || cfg.d % 8)
            throw std::runtime_error("d must be a positive multiple of 8");
        u64 m = X0.rows(), db = cfg.d / 8, n = 0, B = 0;

        ItemMatrix X(m, db);
        {
            std::vector<u64> pi(m);
            for (u64 i = 0; i < m; ++i) pi[i] = i;
            for (u64 i = m; i > 1; --i)
                std::swap(pi[i - 1], pi[prng.get<u64>() % i]);
            for (u64 i = 0; i < m; ++i)
                std::memcpy(&X(i, 0), &X0(pi[i], 0), db);
        }

        // 1. exchange sizes and leakage parameters
        co_await chl.recv(n);
        co_await chl.send(u64(m));
        if (m == 0 || n == 0)
        {
            info = {};
            co_return;
        }
        co_await chl.recv(B);
        if (cfg.leakOnly)
        {
            info = {};
            info.B = B;
            co_return;
        }
        // The fallback decision depends only on public values and B: Q = B * sum_t V is known
        // before any probe is built, so the decision is taken first and, if it falls back,
        // no probes are built and U is never sent.
        u64 Q = p.probesPerElement(B), T = p.numTables();
        u64 N = oc::CuckooIndex<>::selectParams(m * Q, cfg.ssp, 0, cfg.cuckooHashes).numBins();
        u64 dc = p.cmax + (cfg.noCompress ? 0 : p.r);
        u64 tau = tagBits(dc, cfg.delta, N, cfg.ssp);
        if (cfg.keyCheck)
            tau = std::max<u64>(tau, cfg.ssp + oc::log2ceil(N));
        u64 pb = oc::divCeil(dc + tau, 8);
        // In U-free mode the threshold and extended-permutation sizes are m*Q slots.
        if (cfg.allowFallback && preferAllPairs(m, n, cfg.d, cfg.noU ? m * Q : N, dc, tau))
        {
            info = {B, 0, Q, T, p.r, 0, 0, 0, true};
            co_await allPairsCoreSend(cfg, X, n, chl, prng, timer);
            co_return;
        }
        if (cfg.noU)
        {
            co_await senderNoU(cfg, p, X, n, B, Q, T, N, dc, tau, pb, chl, prng, info, timer);
            co_return;
        }
        auto sm = senderMap(p, X, B);
        u64 U = revealedLoad(sm.U, cfg.Ubar, cfg, prng, "U");
        co_await chl.send(u64(U));
        info = {B, U, Q, T, p.r, 0, n * T * U};
        tp(timer, "S map");

        // 2. cuckoo + OPPRF: per-bin XOR shares of the payload
        //    [ y restricted to the complement of table t (cmax bits) | 0^r offset | 0^tau tag ].
        //    A hit certifies y_t = z, so H(x,y) = H(x_t,z) + H(x_{~t}, y_{~t}); S adds H(x_t,z)
        //    ones in the offset field and the circuit counts cmax + r bits.
        auto cpsi = std::make_unique<HamCpsiReceiver>();
        HamCpsiReceiver::Sharing rs;
        cpsi->init(n * T * U, m * Q, pb, cfg.ssp, prng.get(), cfg.threads);
        cpsi->mNumHash = cfg.cuckooHashes;
        co_await cpsi->receive(sm.keys, rs, chl);
        if (rs.mValues.rows() != N)
            throw std::runtime_error("bin count mismatch");
        info.bins = N;
        info.tau = tau;
        tp(timer, "S cpsi");

        // 3. threshold circuit per bin: e = [H(x, y) <= delta] AND [tag == 0]
        BitVector eS;
        {
            Matrix<std::uint8_t> in0(N, pb);
            std::memcpy(in0.data(), rs.mValues.data(), N * pb);
            std::vector<std::uint8_t> buf(pb);
            for (u64 k = 0; k < m * Q; ++k)
            {
                auto bin = rs.mMapping[k];
                MipParams::gather(&X(k / Q, 0), p.complements[sm.table[k]], buf.data(), pb);
                for (u64 e = 0; !cfg.noCompress && e < sm.dist[k]; ++e)
                    buf[(p.cmax + e) >> 3] |= std::uint8_t(1u << ((p.cmax + e) & 7));
                for (u64 t = 0; t < pb; ++t)
                    in0(bin, t) ^= buf[t];
            }
            if (cfg.gmwThreshold)
            {
                auto cir = thresholdCircuit(dc, cfg.delta, tau);
                volePSI::Gmw g;
                g.init(N, cir, cfg.threads, 0, prng.get());
                g.setInput(0, oc::MatrixView<std::uint8_t>(in0));
                co_await g.run(chl);
                eS = outputBits(g, N);
            }
            else
                co_await otThresholdSender(ThresholdShape{dc, tau, cfg.delta}, in0, eS, prng, chl, cfg.threads);
        }
        tp(timer, "S threshold");

        // 4. permute-and-share bins into sender order (position k = i*Q + j)
        BitVector psS;
        {
            std::vector<u64> perm(N);
            std::vector<char> used(N, 0);
            for (u64 k = 0; k < m * Q; ++k)
            {
                perm[k] = rs.mMapping[k];
                used[rs.mMapping[k]] = 1;
            }
            u64 k = m * Q;
            for (u64 bin = 0; bin < N; ++bin)
                if (!used[bin])
                    perm[k++] = bin;
            // R holds its share vector eR; we obtain shares of eR[perm[.]] and add eS[perm[.]]
            co_await osnProgrammer(perm, psS, prng, chl, cfg.threads);
            for (u64 i = 0; i < N; ++i)
                psS[i] = psS[i] ^ eS[perm[i]];
        }
        tp(timer, "S permute");

        // 5. OR over the Q probes of each element
        BitVector oS;
        {
            auto cir = orCircuit(Q);
            volePSI::Gmw g;
            g.init(m, cir, cfg.threads, 0, prng.get());
            auto in = groupBits(psS, m, Q);
            g.setInput(0, oc::MatrixView<std::uint8_t>(in));
            co_await g.run(chl);
            oS = outputBits(g, m);
        }
        tp(timer, "S or");

        // 6. deliver x_i iff e_i = oS ^ oR = 1 (R's choice bit is oR)
        co_await deliverSend(cfg, X, oS, prng, chl);
        tp(timer, "S deliver");
    }

    Proto fpsiReceiver(const FpsiConfig& cfg, const ItemMatrix& Yin, coproto::Socket& chl,
                       oc::PRNG& prng, ItemMatrix& out, FpsiInfo& info, oc::Timer* timer)
    {
        auto p = cfg.coverQ ? MipParams::makeCover(cfg.d, cfg.delta, cfg.coverQ, cfg.publicSeed) : MipParams::make(cfg.d, cfg.delta, cfg.b, cfg.s, cfg.publicSeed);
        if (cfg.noCompress) p.uncompressed();
        auto Y = uniqueRows(Yin); // local dedup (set semantics)
        if (cfg.d == 0 || cfg.d % 8)
            throw std::runtime_error("d must be a positive multiple of 8");
        u64 n = Y.rows(), db = cfg.d / 8, m = 0, U = 0;
        u64 T = p.numTables();

        // Sizes first; an empty input ends the protocol before any load is computed or released.
        co_await chl.send(u64(n));
        co_await chl.recv(m);
        if (m == 0 || n == 0)
        {
            out.resize(0, db);
            info = {};
            co_return;
        }
        auto rm = receiverMap(p, Y, cfg.uniqOnly);
        u64 B = revealedLoad(rm.B, cfg.Bbar, cfg, prng, "B");
        co_await chl.send(u64(B));
        if (cfg.leakOnly)
        {
            info = {};
            info.B = B;
            co_return;
        }
        u64 Q = p.probesPerElement(B);
        u64 N = oc::CuckooIndex<>::selectParams(m * Q, cfg.ssp, 0, cfg.cuckooHashes).numBins();
        u64 dc = p.cmax + (cfg.noCompress ? 0 : p.r);
        u64 tau = tagBits(dc, cfg.delta, N, cfg.ssp);
        if (cfg.keyCheck)
            tau = std::max<u64>(tau, cfg.ssp + oc::log2ceil(N));
        u64 pb = oc::divCeil(dc + tau, 8);
        if (cfg.allowFallback && preferAllPairs(m, n, cfg.d, cfg.noU ? m * Q : N, dc, tau))
        {
            info = {B, 0, Q, T, p.r, 0, 0, 0, true};
            co_await allPairsCoreRecv(cfg, Y, m, chl, prng, out, timer);
            co_return;
        }
        if (cfg.noU)
        {
            co_await receiverNoU(cfg, p, Y, rm, m, B, Q, T, N, dc, tau, pb, chl, prng, out, info, timer);
            co_return;
        }
        co_await chl.recv(U); // only when the index branch is taken
        std::vector<block> keys;
        std::vector<u64> owner;
        receiverKeys(rm, n, T, U, keys, owner);
        // payload of key (j, t, u): y_j restricted to the complement of table t, zero offset/tag
        Matrix<std::uint8_t> vals(keys.size(), pb);
        for (u64 k = 0; k < keys.size(); ++k)
            MipParams::gather(&Y(owner[k], 0), p.complements[(k / U) % T], &vals(k, 0), pb);
        info = {B, U, Q, T, p.r, N, keys.size(), tau};
        tp(timer, "R map");

        auto cpsi = std::make_unique<HamCpsiSender>();
        HamCpsiSender::Sharing ss;
        cpsi->init(keys.size(), m * Q, pb, cfg.ssp, prng.get(), cfg.threads);
        cpsi->mNumHash = cfg.cuckooHashes;
        co_await cpsi->send(keys, vals, ss, chl);
        if (ss.mValues.rows() != N)
            throw std::runtime_error("bin count mismatch");
        tp(timer, "R cpsi");

        BitVector eR;
        {
            if (cfg.gmwThreshold)
            {
                auto cir = thresholdCircuit(dc, cfg.delta, tau);
                volePSI::Gmw g;
                g.init(N, cir, cfg.threads, 1, prng.get());
                g.setInput(0, oc::MatrixView<std::uint8_t>(ss.mValues));
                co_await g.run(chl);
                eR = outputBits(g, N);
            }
            else
                co_await otThresholdReceiver(ThresholdShape{dc, tau, cfg.delta}, ss.mValues, eR, prng, chl, cfg.threads);
        }
        tp(timer, "R threshold");

        BitVector psR;
        {
            co_await osnHolder(eR, psR, prng, chl, cfg.threads);
        }
        tp(timer, "R permute");

        BitVector oR;
        {
            auto cir = orCircuit(Q);
            volePSI::Gmw g;
            g.init(m, cir, cfg.threads, 1, prng.get());
            auto in = groupBits(psR, m, Q);
            g.setInput(0, oc::MatrixView<std::uint8_t>(in));
            co_await g.run(chl);
            oR = outputBits(g, m);
        }
        tp(timer, "R or");

        co_await deliverRecv(cfg, m, oR, out, prng, chl);
        tp(timer, "R deliver");
    }


    bool preferAllPairs(u64 m, u64 n, u64 d, u64 N, u64 dc, u64 tau)
    {
        // MIP: N threshold circuits of ~(dc + tau) ANDs, a switching network with ~log2 N
        // switches per bin (one OT each), and OR gates. All-pairs: m*n circuits of ~d ANDs.
        double mip = double(N) * (double(dc + tau) + std::log2(double(std::max<u64>(N, 2))));
        double all = double(m) * double(n) * double(d);
        return all <= mip;
    }

    // ---- generic all-pairs protocol (baseline, and fallback when it is cheaper) ----
    Proto allPairsCoreSend(const FpsiConfig& cfg, const ItemMatrix& X, u64 n, coproto::Socket& chl,
                           oc::PRNG& prng, oc::Timer* timer)
    {
        u64 m = X.rows(), db = cfg.d / 8;
        // row i*n + j: S's share x_i, R's share y_j  =>  circuit input x_i ^ y_j
        BitVector eS;
        {
            Matrix<std::uint8_t> in(m * n, db);
            for (u64 i = 0; i < m; ++i)
                for (u64 j = 0; j < n; ++j)
                    std::memcpy(&in(i * n + j, 0), &X(i, 0), db);
            auto cir = thresholdCircuit(cfg.d, cfg.delta, 0);
            volePSI::Gmw g;
            g.init(m * n, cir, cfg.threads, 0, prng.get());
            g.setInput(0, oc::MatrixView<std::uint8_t>(in));
            co_await g.run(chl);
            eS = outputBits(g, m * n);
        }
        tp(timer, "S all-pairs threshold");
        BitVector oS;
        {
            auto cir = orCircuit(n);
            volePSI::Gmw g;
            g.init(m, cir, cfg.threads, 0, prng.get());
            auto in = groupBits(eS, m, n);
            g.setInput(0, oc::MatrixView<std::uint8_t>(in));
            co_await g.run(chl);
            oS = outputBits(g, m);
        }
        tp(timer, "S or");
        co_await deliverSend(cfg, X, oS, prng, chl);
        tp(timer, "S deliver");
    }

    Proto allPairsCoreRecv(const FpsiConfig& cfg, const ItemMatrix& Y, u64 m, coproto::Socket& chl,
                           oc::PRNG& prng, ItemMatrix& out, oc::Timer* timer)
    {
        u64 n = Y.rows(), db = cfg.d / 8;
        BitVector eR;
        {
            Matrix<std::uint8_t> in(m * n, db);
            for (u64 i = 0; i < m; ++i)
                std::memcpy(&in(i * n, 0), Y.data(), n * db);
            auto cir = thresholdCircuit(cfg.d, cfg.delta, 0);
            volePSI::Gmw g;
            g.init(m * n, cir, cfg.threads, 1, prng.get());
            g.setInput(0, oc::MatrixView<std::uint8_t>(in));
            co_await g.run(chl);
            eR = outputBits(g, m * n);
        }
        tp(timer, "R all-pairs threshold");
        BitVector oR;
        {
            auto cir = orCircuit(n);
            volePSI::Gmw g;
            g.init(m, cir, cfg.threads, 1, prng.get());
            auto in = groupBits(eR, m, n);
            g.setInput(0, oc::MatrixView<std::uint8_t>(in));
            co_await g.run(chl);
            oR = outputBits(g, m);
        }
        tp(timer, "R or");
        co_await deliverRecv(cfg, m, oR, out, prng, chl);
        tp(timer, "R deliver");
    }

    Proto allPairsSender(const FpsiConfig& cfg, const ItemMatrix& Xin, coproto::Socket& chl,
                         oc::PRNG& prng, oc::Timer* timer)
    {
        auto X = shuffled(uniqueRows(Xin), prng);
        u64 m = X.rows(), n = 0;
        co_await chl.recv(n);
        co_await chl.send(u64(m));
        co_await allPairsCoreSend(cfg, X, n, chl, prng, timer);
    }

    Proto allPairsReceiver(const FpsiConfig& cfg, const ItemMatrix& Yin, coproto::Socket& chl,
                           oc::PRNG& prng, ItemMatrix& out, oc::Timer* timer)
    {
        auto Y = uniqueRows(Yin);
        u64 n = Y.rows(), m = 0;
        co_await chl.send(u64(n));
        co_await chl.recv(m);
        co_await allPairsCoreRecv(cfg, Y, m, chl, prng, out, timer);
    }
}
