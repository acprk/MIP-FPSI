#pragma once
// Added (e3): NATIVE otFPSI on REAL inputs (pairwise output, as in the artifact) and an exact check.
// Same pipeline as the artifact's native benchmark, except that (a) the inputs are read from a data
// file instead of being sampled at random, and (b) the threshold comparison is run on the masked
// distances produced by the distance protocol (the artifact feeds it independent random values), so
// the querier ends with the plaintext m x n match matrix. Querier = sender set X, responder =
// receiver set Y (as in matched.h).
// Enabled by OTFPSI_REAL_DATA=<data.bin> (u64 m, u64 n, u64 d, X rows, Y rows; bit k of a row is
// bit k%8 of byte k/8). Optional OTFPSI_REAL_SAMPLE=<count> (default 200000).
// Check: (1) every one of the m*n pairs against a plaintext popcount brute force (d <= 64), (2) a
// uniformly random sample of OTFPSI_REAL_SAMPLE pairs, (3) all true matches listed separately.
#include "distance_computation/distance_computation.h"
#include "threshold_comparison/threshold_comparison.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

struct RealData
{
    uint64_t m = 0, n = 0, d = 0;
    std::vector<BitVector> X, Y;
    std::vector<uint64_t> xw, yw; // packed rows (d <= 64) for the brute force
};

inline RealData realRead(const char *path, size_t vector_size)
{
    RealData D;
    std::ifstream in(path, std::ios::binary);
    in.read((char *)&D.m, 8);
    in.read((char *)&D.n, 8);
    in.read((char *)&D.d, 8);
    if (!in || D.d != vector_size || D.d > 64 || D.d % 8)
        throw std::runtime_error("data file does not match -vector_size (or d > 64)");
    size_t db = D.d / 8;
    auto rows = [&](size_t cnt, std::vector<BitVector> &V, std::vector<uint64_t> &W)
    {
        V.assign(cnt, BitVector(D.d));
        W.assign(cnt, 0);
        std::vector<uint8_t> buf(db);
        for (size_t i = 0; i < cnt; ++i)
        {
            in.read((char *)buf.data(), db);
            for (size_t k = 0; k < D.d; ++k)
            {
                uint64_t b = (buf[k / 8] >> (k % 8)) & 1;
                V[i][k] = b;
                W[i] |= b << k;
            }
        }
    };
    rows(D.m, D.X, D.xw);
    rows(D.n, D.Y, D.yw);
    if (!in)
        throw std::runtime_error("data file truncated");
    return D;
}

// Compare a match matrix (row-major, querier index * n + responder index) with the plaintext.
inline size_t realCheck(const RealData &D, size_t threshold, const BitVector &got, const char *tag)
{
    const uint64_t m = D.m, n = D.n;
    size_t bad = 0, truePairs = 0, rowsWithMatch = 0, gotPairs = 0;
    std::vector<std::pair<uint32_t, uint32_t>> trueList;
    for (uint64_t i = 0; i < m; ++i)
    {
        bool any = false;
        for (uint64_t j = 0; j < n; ++j)
        {
            bool want = (size_t)__builtin_popcountll(D.xw[i] ^ D.yw[j]) <= threshold;
            bool g = got[i * n + j];
            bad += want != g;
            gotPairs += g;
            if (want)
            {
                ++truePairs;
                any = true;
                trueList.emplace_back((uint32_t)i, (uint32_t)j);
            }
        }
        rowsWithMatch += any;
    }
    // independent re-check on a random sample (BitVector path, as in matched.h) and on all true matches
    size_t sample = 200000;
    if (const char *s = std::getenv("OTFPSI_REAL_SAMPLE"))
        sample = std::strtoull(s, nullptr, 10);
    PRNG prng(osuCrypto::sysRandomSeed());
    size_t sbad = 0, sMatches = 0;
    for (size_t t = 0; t < sample; ++t)
    {
        uint64_t idx = prng.get<uint64_t>() % (m * n);
        uint64_t i = idx / n, j = idx % n;
        size_t h = 0;
        for (size_t k = 0; k < D.d; ++k)
            h += D.X[i][k] ^ D.Y[j][k];
        bool want = h <= threshold;
        sMatches += want;
        sbad += want != (bool)got[idx];
    }
    size_t tbad = 0;
    for (auto [i, j] : trueList)
    {
        size_t h = 0;
        for (size_t k = 0; k < D.d; ++k)
            h += D.X[i][k] ^ D.Y[j][k];
        tbad += !(h <= threshold && got[(uint64_t)i * n + j]);
    }
    std::cout << tag << " full_pairs=" << m * n << " full_mismatches=" << bad
              << " true_pairs=" << truePairs << " got_pairs=" << gotPairs
              << " sender_rows_with_match=" << rowsWithMatch
              << " sample=" << sample << " sample_true=" << sMatches << " sample_mismatches=" << sbad
              << " truematch_checked=" << trueList.size() << " truematch_mismatches=" << tbad << std::endl;
    return bad + sbad + tbad;
}

inline int runRealNative(config cfg)
{
    RealData D = realRead(std::getenv("OTFPSI_REAL_DATA"), cfg.vector_size);
    cfg.querier_size = D.m;
    cfg.responder_size = D.n;
    cfg.threshold_comparison_count = D.m * D.n;
    config dcfg = cfg, tcfg = cfg;
    dcfg.secret_shared = false; // native inputs
    tcfg.secret_shared = false; // native (pairwise) output to the querier
    std::cout << "REAL_NATIVE m=" << D.m << " n=" << D.n << " d=" << D.d << " threshold=" << cfg.threshold << std::endl;

    size_t comm = 0;
    double c0 = processCpuSeconds();
    auto w0 = std::chrono::steady_clock::now();
    std::vector<uint16_t> mask_sums, masked_distances;
    {
        auto thrd = std::thread([&] { mask_sums = runDistanceComputationResponder(D.Y, std::nullopt, dcfg, comm); });
        masked_distances = runDistanceComputationQuerier(D.X, std::nullopt, dcfg);
        thrd.join();
    }
    double c1 = processCpuSeconds();
    auto w1 = std::chrono::steady_clock::now();
    BitVector result;
    {
        auto thrd = std::thread([&] { runThresholdComparisonSender(tcfg, mask_sums, comm); });
        result = runThresholdComparisonReceiver(tcfg, masked_distances);
        thrd.join();
    }
    double c2 = processCpuSeconds();
    auto w2 = std::chrono::steady_clock::now();
    std::cout << "PROTOCOL_CPU distance " << c1 - c0 << std::endl;
    std::cout << "PROTOCOL_CPU threshold " << c2 - c1 << std::endl;
    std::cout << "PROTOCOL_WALL distance " << std::chrono::duration<double>(w1 - w0).count()
              << " threshold " << std::chrono::duration<double>(w2 - w1).count() << std::endl;
    std::cout << "Total Communication: " << comm << " bytes." << std::endl;
    size_t bad = realCheck(D, cfg.threshold, result, "REAL_CHECK");
    std::cout << (bad ? "REAL_VERIFY FAILED" : "REAL_VERIFY OK") << std::endl;
    return bad ? 2 : 0;
}
