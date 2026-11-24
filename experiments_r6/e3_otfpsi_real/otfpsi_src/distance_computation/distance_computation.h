#pragma once

#include "../util/cryptotools.h"
#include "../config.h"
#include "libOTe/Base/BaseOT.h"
#include <cryptoTools/Common/CLP.h>
#include "coproto/Socket/AsioSocket.h"

std::vector<uint16_t> runBatchedDistanceComputationResponder(const std::vector<BitVector> &responder_vectors, coproto::Socket &chl, const config &cfg, size_t &total_communication);
std::vector<uint16_t> runBatchedDistanceComputationQuerier(const std::vector<BitVector> &querier_vectors, coproto::Socket &chl, const config &cfg);
std::vector<uint16_t> runIndividualDistanceComputationResponder(const std::vector<BitVector> &responder_vectors, const std::optional<std::vector<BitVector>> &querier_shares, coproto::Socket &chl, const config &cfg, size_t &total_communication);
std::vector<uint16_t> runIndividualDistanceComputationQuerier(const std::vector<BitVector> &querier_vectors, const std::optional<std::vector<BitVector>> &responder_shares, coproto::Socket &chl, const config &cfg);
std::vector<uint16_t> runSharedBatchedDistanceComputationResponder(const std::vector<BitVector> &responder_vectors, const std::optional<std::vector<BitVector>> &querier_shares, coproto::Socket &chl, const config &cfg, size_t &total_communication);
std::vector<uint16_t> runSharedBatchedDistanceComputationQuerier(const std::vector<BitVector> &querier_vectors, const std::optional<std::vector<BitVector>> &responder_shares, coproto::Socket &chl, const config &cfg);


inline std::vector<uint16_t> runDistanceComputationResponder(const std::vector<BitVector> &responder_vectors, const std::optional<std::vector<BitVector>> &querier_shares, const config &cfg, size_t &total_communication)
{
    auto chl = localConnect(true); // e3: was osuCrypto::cp::asioConnect(cfg.ip, true)

    if (!cfg.distance_computation.has_value())
        throw std::runtime_error("Distance Computation not enabled");

    std::vector<uint16_t> r;

    if (cfg.distance_computation.value() == VAL_DISTANCE_COMPUTATION_BATCHED)
    {
        assert(!cfg.secret_shared && !querier_shares.has_value());
        r = runBatchedDistanceComputationResponder(responder_vectors, chl, cfg, total_communication);
    }
    else if (cfg.distance_computation.value() == VAL_DISTANCE_COMPUTATION_INDIVIDUAL)
    {
        r = runIndividualDistanceComputationResponder(responder_vectors, querier_shares, chl, cfg, total_communication);
    }
    else if (cfg.distance_computation.value() == VAL_DISTANCE_COMPUTATION_SHARED_BATCHED)
    {
        r = runSharedBatchedDistanceComputationResponder(responder_vectors, querier_shares, chl, cfg, total_communication);
    }
    else
    {
        throw std::runtime_error("Unknown Distance Computation Protocol " + cfg.distance_computation.value());
    }

    return r;
}

inline std::vector<uint16_t> runDistanceComputationQuerier(const std::vector<BitVector> &querier_vectors, const std::optional<std::vector<BitVector>> &responder_shares, const config &cfg)
{
    auto chl = localConnect(false); // e3: was osuCrypto::cp::asioConnect(cfg.ip, false)

    if (!cfg.distance_computation.has_value())
        throw std::runtime_error("Distance Computation not enbaled");

    std::vector<uint16_t> r;
    if (cfg.distance_computation.value() == VAL_DISTANCE_COMPUTATION_BATCHED)
    {
        assert(!cfg.secret_shared && !responder_shares.has_value());
        r = runBatchedDistanceComputationQuerier(querier_vectors, chl, cfg);
    }
    else if (cfg.distance_computation.value() == VAL_DISTANCE_COMPUTATION_INDIVIDUAL)
    {
        r = runIndividualDistanceComputationQuerier(querier_vectors, responder_shares, chl, cfg);
    }
    else if (cfg.distance_computation.value() == VAL_DISTANCE_COMPUTATION_SHARED_BATCHED)
    {
        r = runSharedBatchedDistanceComputationQuerier(querier_vectors, responder_shares, chl, cfg);
    }
    else
    {
        throw std::runtime_error("Unknown Distance Computation Protocol " + cfg.distance_computation.value());
    }

    return r;
}

inline std::tuple<std::vector<BitVector>, std::vector<BitVector>, std::optional<std::vector<BitVector>>, std::optional<std::vector<BitVector>>> generateRandomData(const config &cfg, PRNG &prng)
{

    std::vector<BitVector> querier_data(cfg.querier_size, BitVector(cfg.vector_size));
    std::vector<BitVector> responder_data(cfg.responder_size, BitVector(cfg.vector_size));

    for (size_t i = 0; i < cfg.querier_size; ++i)
    {
        querier_data[i].randomize(prng);
    }
    for (size_t i = 0; i < cfg.responder_size; ++i)
    {
        responder_data[i].randomize(prng);
    }

    if (!cfg.secret_shared)
    {
        // If not secret-shared, we return the data as is
        return {querier_data, responder_data, std::nullopt, std::nullopt};
    }

    std::vector<BitVector> querier_shares(cfg.querier_size, BitVector(cfg.vector_size));
    std::vector<BitVector> responder_shares(cfg.responder_size, BitVector(cfg.vector_size));
    for (size_t i = 0; i < cfg.querier_size; ++i)
    {
        querier_shares[i].randomize(prng);
    }
    for (size_t i = 0; i < cfg.responder_size; ++i)
    {
        responder_shares[i].randomize(prng);
    }

    return {querier_data, responder_data, querier_shares, responder_shares};
}

inline bool verifyDistanceComputationResults(const std::vector<BitVector> &responder_data,
                                             const std::vector<BitVector> &querier_data,
                                             const std::optional<std::vector<BitVector>> &responder_shares,
                                             const std::optional<std::vector<BitVector>> &querier_shares,
                                             const std::vector<uint16_t> &masked_distances,
                                             const std::vector<uint16_t> &mask_sums,
                                             config &cfg)
{

    assert(querier_data.size() == cfg.querier_size);
    assert(responder_data.size() == cfg.responder_size);
    assert(masked_distances.size() == cfg.querier_size * cfg.responder_size);
    assert(mask_sums.size() == cfg.querier_size * cfg.responder_size);
    assert(cfg.secret_shared == querier_shares.has_value());
    assert(cfg.secret_shared == responder_shares.has_value());

    for (size_t querier_index = 0; querier_index < querier_data.size(); ++querier_index)
    {
        for (size_t responder_index = 0; responder_index < responder_data.size(); ++responder_index)
        {
            // Compute Hamming distance between querier_data[querier_index] and responder_data[responder_index]
            int64_t hamming_distance = 0;
            for (size_t i = 0; i < cfg.vector_size; ++i)
            {
                size_t bit = querier_data[querier_index][i] ^ responder_data[responder_index][i];
                if (cfg.secret_shared)
                {
                    bit ^= (*querier_shares)[querier_index][i];
                    bit ^= (*responder_shares)[responder_index][i];
                }
                hamming_distance += bit;
            }

            size_t comparison_index = querier_index * responder_data.size() + responder_index;

            int64_t masked_distance = masked_distances[comparison_index];
            int64_t mask_sum = mask_sums[comparison_index];

            int64_t distance = MOD(masked_distance - mask_sum, cfg.modulus);

            if (distance != hamming_distance)
            {
                std::cout << "==================== MISMATCH DETECTED ====================" << std::endl;
                std::cout << "Mismatch in Hamming distance for query " << querier_index << " and responder " << responder_index << ": "
                          << "Expected " << hamming_distance << ", "
                          << "Got " << distance << ". Delta: " << (hamming_distance - distance) << ". Mask: " << mask_sum << ", Masked Distance: " << masked_distance << std::endl;
                std::cout << "==========================================================" << std::endl;
                return false;
            }
            else
            {
                // std::cout << "Querier " << querier_index << ", Responder " << responder_index
                //          << ": Hamming distance matches: " << hamming_distance << std::endl;
            }
        }
    }
    return true;
}

inline bool runDistanceComputationProtocol(config &cfg, Timer &timer, size_t &total_communication)
{
    // Generate two random vectors
    PRNG prng(osuCrypto::sysRandomSeed());

    auto [querier_data, responder_data, querier_shares, responder_shares] = generateRandomData(cfg, prng);

    if (cfg.run_sender && !cfg.run_receiver)
    {
        runDistanceComputationResponder(responder_data, querier_shares, cfg, total_communication);
    }
    else if (cfg.run_receiver && !cfg.run_sender)
    {
        runDistanceComputationQuerier(querier_data, responder_shares, cfg);
    }

    // Otherwise, we run both
    else
    {
        timer.setTimePoint("start");
        double cpu0 = processCpuSeconds();
        std::vector<uint16_t> mask_sums, masked_distances;

        auto thrd = std::thread([&]
                                {
                try { mask_sums = runDistanceComputationResponder(responder_data, querier_shares, cfg, total_communication); }
                catch (std::exception& e)
                {
                    std::cout << e.what() << std::endl;
                } });

        try
        {
            masked_distances = runDistanceComputationQuerier(querier_data, responder_shares, cfg);
        }
        catch (std::exception &e)
        {
            std::cout << e.what() << std::endl;
        }
        thrd.join();
        timer.setTimePoint("Interleaved Distance Computation");
        std::cout << "PROTOCOL_CPU distance " << processCpuSeconds() - cpu0 << std::endl;

        // Verify the results
        if (!verifyDistanceComputationResults(responder_data, querier_data, responder_shares, querier_shares, masked_distances, mask_sums, cfg))
        {
            std::cout << "Verification failed!" << std::endl;
        }
        else
        {
            std::cout << "Verification succeeded!" << std::endl;
        }
    }

    return true;
}
