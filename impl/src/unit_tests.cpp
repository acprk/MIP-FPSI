// Unit tests: circuits (plaintext), MIP zero-false-negative property, radius formula.
#include "circuits.h"
#include "mip.h"
#include "osn.h"
#include "oep.h"
#include "fpsi.h"

#include <coproto/Socket/LocalAsyncSock.h>
#include <macoro/sync_wait.h>
#include <macoro/when_all.h>
#include <functional>

#include <cryptoTools/Crypto/PRNG.h>
#include <iostream>
#include <set>

using namespace mip;

static void testRadius()
{
    // brute force: max over integer block-distance vectors (sum <= delta) of sum of s smallest
    for (u64 b = 1; b <= 8; ++b)
        for (u64 s = 1; s <= b; ++s)
            for (u64 delta = 0; delta <= 12; ++delta)
            {
                u64 best = 0;
                std::vector<u64> v(b, 0);
                std::function<void(u64, u64)> rec = [&](u64 i, u64 left) {
                    if (i == b)
                    {
                        auto w = v;
                        std::sort(w.begin(), w.end());
                        u64 sum = 0;
                        for (u64 k = 0; k < s; ++k) sum += w[k];
                        best = std::max(best, sum);
                        return;
                    }
                    for (u64 x = 0; x <= left; ++x) { v[i] = x; rec(i + 1, left - x); }
                };
                rec(0, delta);
                if (best != MipParams::radius(b, s, delta))
                    throw std::runtime_error("radius formula wrong b=" + std::to_string(b) +
                                             " s=" + std::to_string(s) + " delta=" + std::to_string(delta));
            }
    std::cout << "radius formula: ok\n";
}

static void testMipNoFalseNegative()
{
    oc::PRNG prng(oc::block(3, 3));
    struct C { u64 d, delta, b, s; };
    for (auto c : {C{64, 3, 4, 1}, C{64, 3, 6, 3}, C{128, 4, 5, 1}, C{128, 4, 3, 1}, C{128, 6, 4, 2}, C{256, 8, 9, 1}})
    {
        auto p = MipParams::make(c.d, c.delta, c.b, c.s, oc::block(1, 2));
        u64 n = 300, db = c.d / 8;
        ItemMatrix Y(n, db), X(n, db);
        prng.get(Y.data(), Y.size());
        for (u64 j = 0; j < n; ++j)
        {
            std::memcpy(&X(j, 0), &Y(j, 0), db);
            u64 w = prng.get<u64>() % (c.delta + 1);
            std::set<u64> flips;
            while (flips.size() < w) flips.insert(prng.get<u64>() % c.d);
            for (auto bit : flips) X(j, bit / 8) ^= std::uint8_t(1u << (bit % 8));
        }
        auto rm = receiverMap(p, Y);
        auto sm = senderMap(p, X, rm.B);
        std::vector<block> rk; std::vector<u64> owner;
        receiverKeys(rm, n, p.numTables(), sm.U, rk, owner);
        std::set<std::pair<u64, u64>> rset;
        for (u64 k = 0; k < rk.size(); ++k)
            rset.insert({rk[k].get<u64>()[0], rk[k].get<u64>()[1]});
        std::set<std::pair<u64, u64>> sset;
        for (auto& k : sm.keys)
            if (!sset.insert({k.get<u64>()[0], k.get<u64>()[1]}).second)
                throw std::runtime_error("sender keys not distinct");
        // every X[j] must hit at least one key owned by Y[j]
        for (u64 j = 0; j < n; ++j)
        {
            std::set<std::pair<u64, u64>> own;
            for (u64 k = 0; k < rk.size(); ++k)
                if (owner[k] == j) own.insert({rk[k].get<u64>()[0], rk[k].get<u64>()[1]});
            bool hit = false;
            for (u64 q = 0; q < sm.Q && !hit; ++q)
            {
                auto& k = sm.keys[j * sm.Q + q];
                hit = own.count({k.get<u64>()[0], k.get<u64>()[1]});
            }
            if (!hit)
                throw std::runtime_error("false negative in MIP");
        }
        std::cout << "MIP d=" << c.d << " delta=" << c.delta << " b=" << c.b << " s=" << c.s
                  << " r=" << p.r << " T=" << p.numTables() << " B=" << rm.B << " U=" << sm.U
                  << " Q=" << sm.Q << ": no false negative\n";
    }
}

static void testOsn()
{
    oc::PRNG prng(oc::block(5, 5));
    auto randPerm = [&](u64 N) {
        std::vector<u64> p(N);
        for (u64 i = 0; i < N; ++i) p[i] = i;
        for (u64 i = N; i > 1; --i) std::swap(p[i - 1], p[prng.get<u64>() % i]);
        return p;
    };
    for (u64 N : {1ull, 2ull, 3ull, 4ull, 5ull, 8ull, 17ull, 100ull, 1000ull, 4096ull, 5000ull})
        for (u64 t = 0; t < 5; ++t)
        {
            auto p = randPerm(N);
            auto net = BenesNetwork::build(N, p);
            oc::BitVector x(N);
            x.randomize(prng);
            auto y = net.eval(x);
            for (u64 k = 0; k < N; ++k)
                if (y[k] != x[p[k]])
                    throw std::runtime_error("benes routing wrong N=" + std::to_string(N));
        }
    // arbitrary-size (Waksman) networks: every size 1..96, 30 random permutations each
    for (u64 N = 1; N <= 96; ++N)
        for (u64 t = 0; t < 30; ++t)
        {
            auto p = randPerm(N);
            auto net = BenesNetwork::build(N, p);
            if (net.np != N)
                throw std::runtime_error("benes padded unexpectedly N=" + std::to_string(N));
            oc::BitVector x(N);
            x.randomize(prng);
            auto y = net.eval(x);
            for (u64 k = 0; k < N; ++k)
                if (y[k] != x[p[k]])
                    throw std::runtime_error("arbitrary-size benes routing wrong N=" + std::to_string(N));
        }
    std::cout << "benes routing: ok\n";
    for (u64 N : {1ull, 2ull, 7ull, 1000ull, 3001ull})
    {
        auto p = randPerm(N);
        oc::BitVector x(N), s0, s1;
        x.randomize(prng);
        auto socks = coproto::LocalAsyncSocket::makePair();
        oc::PRNG p0(prng.get()), p1(prng.get());
        auto r = macoro::sync_wait(macoro::when_all_ready(
            osnProgrammer(p, s0, p0, socks[0]), osnHolder(x, s1, p1, socks[1])));
        std::get<0>(r).result();
        std::get<1>(r).result();
        for (u64 k = 0; k < N; ++k)
            if ((s0[k] ^ s1[k]) != x[p[k]])
                throw std::runtime_error("osn protocol wrong N=" + std::to_string(N));
        // shares must not be trivially equal to the plaintext on the holder side
        if (N >= 1000 && s1 == x)
            throw std::runtime_error("osn holder share degenerate");
    }
    std::cout << "osn protocol: ok\n";
}

static void testOep()
{
    oc::PRNG prng(oc::block(6, 6));
    for (u64 N : {5ull, 64ull, 1000ull, 4097ull})
        for (u64 w : {1ull, 13ull, 20ull})
        {
            // M <= N outputs with duplicates (skewed: half of the outputs hit few inputs)
            for (u64 M : {N, (N + 1) / 2, u64(1)})
            {
                std::vector<u64> map(M);
                for (u64 j = 0; j < M; ++j)
                    map[j] = (j % 2) ? prng.get<u64>() % N : prng.get<u64>() % std::max<u64>(1, N / 16);
                oc::Matrix<std::uint8_t> x(N, w), s0, s1;
                prng.get(x.data(), x.size());
                auto net = ExtPermNetwork::build(N, M, map);
                auto y = net.eval(x);
                for (u64 j = 0; j < M; ++j)
                    if (std::memcmp(&y(j, 0), &x(map[j], 0), w))
                        throw std::runtime_error("ext-perm routing wrong N=" + std::to_string(N));
                auto socks = coproto::LocalAsyncSocket::makePair();
                oc::PRNG p0(prng.get()), p1(prng.get());
                auto r = macoro::sync_wait(macoro::when_all_ready(
                    oepProgrammer(map, N, w, s0, p0, socks[0]), oepHolder(x, M, s1, p1, socks[1])));
                std::get<0>(r).result();
                std::get<1>(r).result();
                bool holderEqual = true;
                for (u64 j = 0; j < M; ++j)
                    for (u64 t = 0; t < w; ++t)
                    {
                        if ((s0(j, t) ^ s1(j, t)) != x(map[j], t))
                            throw std::runtime_error("oep protocol wrong N=" + std::to_string(N) +
                                                     " w=" + std::to_string(w) + " M=" + std::to_string(M));
                        holderEqual &= s1(j, t) == x(map[j], t);
                    }
                if (M * w >= 64 && holderEqual)
                    throw std::runtime_error("oep holder share degenerate");
            }
        }
    std::cout << "oep (extended permutation, w in {1,13,20}): ok\n";
}

// End-to-end U-free FPSI against brute force, including clustered inputs (many vectors share
// block keys, so the base-key multiplicity U would be > 1 in the default mode).
static void testNoUEndToEnd()
{
    oc::PRNG prng(oc::block(8, 8));
    struct C { u64 d, delta, b, s; bool clustered; };
    for (auto c : {C{128, 4, 5, 1, false}, C{128, 4, 5, 1, true}, C{64, 3, 4, 1, false}, C{64, 3, 4, 1, true}})
        for (int dpMode = 0; dpMode < 2; ++dpMode)
        {
            u64 m = 300, n = 300, db = c.d / 8;
            ItemMatrix X(m, db), Y(n, db);
            prng.get(Y.data(), Y.size());
            if (c.clustered)
                // 6 clusters: rows equal to a centre except in the last byte
                for (u64 j = 0; j < n; ++j)
                {
                    std::vector<std::uint8_t> centre(db);
                    oc::PRNG cp(oc::block(77, j % 6));
                    cp.get(centre.data(), db);
                    centre[db - 1] = std::uint8_t(prng.get<u64>());
                    std::memcpy(&Y(j, 0), centre.data(), db);
                }
            prng.get(X.data(), X.size());
            for (u64 i = 0; i < m / 2; ++i)
            {
                std::memcpy(&X(i, 0), &Y(prng.get<u64>() % n, 0), db);
                u64 w = prng.get<u64>() % (c.delta + 3);
                for (u64 e = 0; e < w; ++e)
                {
                    u64 bit = prng.get<u64>() % c.d;
                    X(i, bit / 8) ^= std::uint8_t(1u << (bit % 8));
                }
            }
            auto Xu = uniqueRows(X), Yu = uniqueRows(Y);
            std::set<std::vector<std::uint8_t>> truth, got;
            for (u64 i = 0; i < Xu.rows(); ++i)
                for (u64 j = 0; j < Yu.rows(); ++j)
                    if (hamming(&Xu(i, 0), &Yu(j, 0), c.d) <= c.delta)
                    {
                        truth.insert(std::vector<std::uint8_t>(&Xu(i, 0), &Xu(i, 0) + db));
                        break;
                    }
            FpsiConfig cfg;
            cfg.d = c.d; cfg.delta = c.delta; cfg.b = c.b; cfg.s = c.s;
            cfg.noU = true;
            cfg.allowFallback = false;
            if (dpMode) cfg.dpEps = 1.0;
            FpsiInfo is, ir;
            ItemMatrix out;
            auto socks = coproto::LocalAsyncSocket::makePair();
            oc::PRNG ps(prng.get()), pr(prng.get());
            auto r = macoro::sync_wait(macoro::when_all_ready(
                fpsiSender(cfg, X, socks[0], ps, is, nullptr),
                fpsiReceiver(cfg, Y, socks[1], pr, out, ir, nullptr)));
            std::get<0>(r).result();
            std::get<1>(r).result();
            for (u64 i = 0; i < out.rows(); ++i)
                got.insert(std::vector<std::uint8_t>(&out(i, 0), &out(i, 0) + db));
            // default-mode multiplicity for comparison
            auto p = MipParams::make(c.d, c.delta, c.b, c.s, cfg.publicSeed);
            auto rm = receiverMap(p, Yu);
            auto smDefault = senderMap(p, Xu, rm.B);
            if (got != truth)
                throw std::runtime_error("noU FPSI output wrong d=" + std::to_string(c.d));
            if (is.U != 1 || ir.programmed != Yu.rows() * p.numTables())
                throw std::runtime_error("noU info wrong");
            std::cout << "noU FPSI d=" << c.d << " delta=" << c.delta << (c.clustered ? " clustered" : " uniform")
                      << (dpMode ? " dp(eps=1)" : "") << ": B=" << rm.B << " revealedB=" << ir.B
                      << " (default-mode U would be " << smDefault.U << ") programmed=" << ir.programmed
                      << " |I|=" << truth.size() << ": exact\n";
        }
}

static void testCoverNoFalseNegative()
{
    oc::PRNG prng(oc::block(9, 9));
    struct C { u64 d, delta, q; };
    for (auto c : {C{64, 3, 1}, C{64, 8, 2}, C{128, 4, 1}, C{128, 16, 3}, C{128, 16, 4}})
    {
        auto p = MipParams::makeCover(c.d, c.delta, c.q, oc::block(4, 4));
        // worst case: exactly delta differing positions, many random trials
        for (u64 t = 0; t < 3000; ++t)
        {
            std::set<u64> D;
            while (D.size() < c.delta) D.insert(prng.get<u64>() % c.d);
            bool covered = false;
            for (auto& tab : p.tables)
            {
                bool hit = false;
                for (auto pos : tab) hit |= D.count(pos) > 0;
                if (!hit) { covered = true; break; }
            }
            if (!covered)
                throw std::runtime_error("cover failed");
        }
        std::cout << "cover d=" << c.d << " delta=" << c.delta << " q=" << c.q << " T=" << p.numTables()
                  << ": covering property holds on 3000 worst-case difference sets\n";
    }
}

int main()
{
    testOsn();
    testOep();
    testNoUEndToEnd();
    testCoverNoFalseNegative();
    circuitSelfTest();
    std::cout << "circuits: ok\n";
    testRadius();
    testMipNoFalseNegative();
    return 0;
}
