#pragma once
// Added for the FPSI comparison (output-matched otFPSI, stage 1).
// Runs otFPSI's native batched distance computation on REAL inputs (querier = sender set X,
// responder = receiver set Y), feeds the masked distances into its threshold comparison in
// secret-shared-output mode, and writes each party's XOR share of the m x n comparison bits to a
// file. Stage 2 (OR per sender vector + OT delivery) is or_deliver_local (e3) / impl or_deliver.
// Enabled by the environment variable OTFPSI_MATCHED_DATA=<data.bin> (format: u64 m, u64 n,
// u64 d, X rows, Y rows; bit k of a row is bit k%8 of byte k/8). Output files:
// $OTFPSI_MATCHED_OUT.S (querier share) and $OTFPSI_MATCHED_OUT.R (responder share).
// e3 changes: (1) the sender's shuffle: if OTFPSI_MATCHED_SHUFFLED_DATA=<path> is set, the sender
// permutes the rows of X with a fresh secret uniform permutation (Fisher-Yates, as in our protocol's
// step 0) BEFORE stage 1, and writes the data file in the permuted order for stage 2, so the order of
// the delivered outputs reveals nothing about the sender's original order (the OR/delivery rows are
// indexed by sender row; the shuffle has to precede stage 1 so that both parties' share rows refer to
// the same permuted order without an oblivious-shuffle subprotocol). The shuffle time is inside the
// measured protocol CPU. (2) exact check of ALL m*n shared comparison bits + random sample + all
// true matches (realCheck), instead of a 200,000-pair sample only.
#include "realinput.h"

inline int runMatchedStage1(config cfg)
{
    const char *path = std::getenv("OTFPSI_MATCHED_DATA");
    const char *outp = std::getenv("OTFPSI_MATCHED_OUT");
    const char *shuf = std::getenv("OTFPSI_MATCHED_SHUFFLED_DATA");
    if (!path || !outp)
        throw std::runtime_error("OTFPSI_MATCHED_DATA and OTFPSI_MATCHED_OUT must be set");
    RealData D = realRead(path, cfg.vector_size);
    const uint64_t m = D.m, n = D.n, d = D.d;

    double c0 = processCpuSeconds();
    if (shuf)
    {
        PRNG sp(osuCrypto::sysRandomSeed()); // sender's private randomness
        std::vector<uint64_t> pi(m);
        for (uint64_t i = 0; i < m; ++i)
            pi[i] = i;
        for (uint64_t i = m; i > 1; --i)
            std::swap(pi[i - 1], pi[sp.get<uint64_t>() % i]);
        std::vector<BitVector> X2(m);
        std::vector<uint64_t> xw2(m);
        for (uint64_t i = 0; i < m; ++i)
        {
            X2[i] = D.X[pi[i]];
            xw2[i] = D.xw[pi[i]];
        }
        D.X.swap(X2);
        D.xw.swap(xw2);
        // the sender's (permuted) input for stage 2; Y unchanged
        std::ofstream f(shuf, std::ios::binary);
        f.write((const char *)&m, 8); f.write((const char *)&n, 8); f.write((const char *)&d, 8);
        for (uint64_t i = 0; i < m; ++i)
            for (uint64_t k = 0; k < d / 8; ++k) { uint8_t b = (D.xw[i] >> (8 * k)) & 0xff; f.write((const char *)&b, 1); }
        for (uint64_t j = 0; j < n; ++j)
            for (uint64_t k = 0; k < d / 8; ++k) { uint8_t b = (D.yw[j] >> (8 * k)) & 0xff; f.write((const char *)&b, 1); }
        std::cout << "SENDER_SHUFFLE applied, permuted data written to " << shuf << std::endl;
    }
    double cs = processCpuSeconds();

    cfg.querier_size = m;
    cfg.responder_size = n;
    cfg.threshold_comparison_count = m * n;
    config dcfg = cfg, tcfg = cfg;
    dcfg.secret_shared = false; // native inputs
    tcfg.secret_shared = true;  // shared comparison outputs

    size_t comm = 0;
    double c0b = processCpuSeconds();
    auto w0 = std::chrono::steady_clock::now();
    std::vector<uint16_t> mask_sums, masked_distances;
    {
        auto thrd = std::thread([&] { mask_sums = runDistanceComputationResponder(D.Y, std::nullopt, dcfg, comm); });
        masked_distances = runDistanceComputationQuerier(D.X, std::nullopt, dcfg);
        thrd.join();
    }
    double c1 = processCpuSeconds();
    auto w1 = std::chrono::steady_clock::now();
    std::optional<BitVector> shareR;
    BitVector shareS;
    {
        auto thrd = std::thread([&] { shareR = runThresholdComparisonSender(tcfg, mask_sums, comm); });
        shareS = runThresholdComparisonReceiver(tcfg, masked_distances);
        thrd.join();
    }
    double c2 = processCpuSeconds();
    auto w2 = std::chrono::steady_clock::now();
    std::cout << "PROTOCOL_CPU shuffle " << cs - c0 << std::endl;
    std::cout << "PROTOCOL_CPU distance " << c1 - c0b << std::endl;
    std::cout << "PROTOCOL_CPU threshold " << c2 - c1 << std::endl;
    std::cout << "PROTOCOL_WALL distance " << std::chrono::duration<double>(w1 - w0).count()
              << " threshold " << std::chrono::duration<double>(w2 - w1).count() << std::endl;
    std::cout << "Total Communication: " << comm << " bytes." << std::endl;

    // write shares (row-major: querier index * n + responder index)
    std::ofstream fs(std::string(outp) + ".S", std::ios::binary), fr(std::string(outp) + ".R", std::ios::binary);
    fs.write((const char *)&m, 8); fs.write((const char *)&n, 8);
    fr.write((const char *)&m, 8); fr.write((const char *)&n, 8);
    fs.write((const char *)shareS.data(), shareS.sizeBytes());
    fr.write((const char *)shareR->data(), shareR->sizeBytes());
    fs.close();
    fr.close();

    BitVector got = shareS;
    got ^= *shareR;
    size_t bad = realCheck(D, cfg.threshold, got, "MATCHED_CHECK");
    std::cout << "MATCHED_CHECK checked=all mismatches=" << bad << std::endl;
    return bad ? 2 : 0;
}
