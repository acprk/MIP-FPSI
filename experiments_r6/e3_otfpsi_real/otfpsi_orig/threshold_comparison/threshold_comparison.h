#pragma once

#include "../util/cryptotools.h"
#include "../config.h"
#include <vector>
#include <string>
#include <stdint.h>
#include <cryptoTools/Common/CLP.h>
#include "libOTe/Tools/Coproto.h"
#include "coproto/Socket/AsioSocket.h"

BitVector runNpThresholdComparisonReceiver(const config &cfg, coproto::AsioSocket &chl, const std::vector<uint16_t> &maskedDistances);
std::optional<BitVector> runNpThresholdComparisonSender(const config &cfg, coproto::AsioSocket &chl, const std::vector<uint16_t> &masks, size_t &total_communication);


inline BitVector runThresholdComparisonReceiver(const config &cfg, const std::vector<uint16_t> &maskedDistances)
{
    assert(cfg.threshold_comparison_count == maskedDistances.size());

    // get up the networking
    auto chl = osuCrypto::cp::asioConnect(cfg.ip, false);

    if (!cfg.threshold_comparison.has_value())
        throw std::runtime_error("Threshold Comparison not enabled");

    BitVector results;

    if (cfg.threshold_comparison.value() == VAL_THRESHOLD_COMPARISON_NP)
        results = runNpThresholdComparisonReceiver(cfg, chl, maskedDistances);        
    else
        throw std::runtime_error("Unknown Threshold Comparison Protocol " + cfg.threshold_comparison.value());
        
    return results;
}

inline std::optional<BitVector> runThresholdComparisonSender(const config &cfg, const std::vector<uint16_t> &masks, size_t &total_communication)
{
    assert(cfg.threshold_comparison_count == masks.size());

    // get up the networking
    auto chl = osuCrypto::cp::asioConnect(cfg.ip, true);

    if (!cfg.threshold_comparison.has_value())
        throw std::runtime_error("Threshold Comparison not enabled");

    if (cfg.threshold_comparison.value() == VAL_THRESHOLD_COMPARISON_NP)
        return runNpThresholdComparisonSender(cfg, chl, masks, total_communication);
    else
        throw std::runtime_error("Unknown Threshold Comparison Protocol " + cfg.threshold_comparison.value());

    return std::nullopt;
}

inline std::tuple<std::vector<uint16_t>, std::vector<uint16_t>> generateRandomDataThresholdComparison(size_t noThresholdComparisons, size_t modulus, PRNG &prng)
{
    std::vector<uint16_t> maskedDistances(noThresholdComparisons);
    std::vector<uint16_t> masks(noThresholdComparisons);

    for (size_t i = 0; i < noThresholdComparisons; ++i)
    {
        maskedDistances[i] = prng.get<uint16_t>() % modulus; // Random values for receiver
        masks[i] = prng.get<uint16_t>() % modulus;           // Random values for sender
    }

    return {maskedDistances, masks};
}

inline bool checkThresholdComparisonResults(const config &cfg, const std::vector<uint16_t> &maskedDistances, const std::vector<uint16_t> &masks, const BitVector &results, const std::optional<BitVector> &result_shares)
{
    assert(maskedDistances.size() == cfg.threshold_comparison_count);
    assert(masks.size() == cfg.threshold_comparison_count);
    assert(results.size() == cfg.threshold_comparison_count);
    assert(!cfg.secret_shared || (result_shares.has_value() && result_shares->size() == cfg.threshold_comparison_count));

    // Verify the results

    for (size_t i = 0; i < maskedDistances.size(); ++i)
    {
        int maskedDistance = maskedDistances[i];
        int mask = masks[i];
        int32_t distance = MOD(maskedDistance - mask, cfg.modulus);
        if (distance < 0)
        {
            distance += cfg.modulus;
        }
        bool expectedResult = (distance <= ((int32_t)cfg.threshold));
        bool result = results[i];
        if (cfg.secret_shared)
        {
            result ^= (*result_shares)[i];
        }

        if (result != expectedResult)
        {
            std::cout << "Mismatch at index " << i << ": expected " << expectedResult << ", got " << results[i] << ". Distance: " << distance << std::endl;
            return false;
        }
    }
    return true;
}

inline bool runThresholdComparisonProtocol(const config &cfg, Timer &timer, size_t &total_communication)
{
    // Generate two random vectors
    PRNG prng(osuCrypto::sysRandomSeed());

    auto [maskedDistances, masks] = generateRandomDataThresholdComparison(cfg.threshold_comparison_count, cfg.modulus, prng);

    if (cfg.run_sender && !cfg.run_receiver)
    {
        runThresholdComparisonSender(cfg, masks, total_communication);
    }
    else if (cfg.run_receiver && !cfg.run_sender)
    {
        runThresholdComparisonReceiver(cfg, maskedDistances);
    }

    else // Else, run both
    {
        timer.setTimePoint("start");
        double cpu0 = processCpuSeconds();
        BitVector comparison_results;
        std::optional<BitVector> result_shares;

        auto thrd = std::thread([&]
                                {
                try { result_shares = runThresholdComparisonSender(cfg, masks, total_communication); }
                catch (std::exception& e)
                {
                    std::cout << e.what() << std::endl;
                } });

        try
        {
            comparison_results = runThresholdComparisonReceiver(cfg, maskedDistances);
        }
        catch (std::exception &e)
        {
            std::cout << e.what() << std::endl;
        }
        thrd.join();
        timer.setTimePoint("Interleaved Threshold Comparison");
        std::cout << "PROTOCOL_CPU threshold " << processCpuSeconds() - cpu0 << std::endl;

        // Verify the results
        if (!checkThresholdComparisonResults(cfg, maskedDistances, masks, comparison_results, result_shares))
        {
            std::cout << "Verification failed!" << std::endl;
            return false;
        }
        else
        {
            std::cout << "Verification succeeded!" << std::endl;
        }
    }

    return true;
}
