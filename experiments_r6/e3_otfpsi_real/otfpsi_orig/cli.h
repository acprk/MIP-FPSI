#pragma once

#include "config.h"
#include "cryptoTools/Common/CLP.h"
#include <cmath>

#define KEY_QUERIER_SIZE "querier_size"
#define KEY_RESPONDER_SIZE "responder_size"
#define KEY_VECTOR_SIZE "vector_size"
#define KEY_THRESHOLD "threshold"
#define KEY_IP "localhost:1212"
#define KEY_DISTANCE_COMPUTATION "proto_distance"
#define KEY_THRESHOLD_COMPARISON "proto_threshold"
#define KEY_RUN_SENDER "run_sender"
#define KEY_RUN_RECEIVER "run_receiver"
#define KEY_OT "ot"
#define KEY_PRF "prf"
#define KEY_SECRET_SHARED "secret_shared"
#define KEY_HELP "help"

inline void print_help()
{
    std::cout << "Usage: otfpsi [options]" << std::endl;
    std::cout << "Options:" << std::endl;

    std::cout << "\nGeneral:" << std::endl;
    std::cout << "  -" << KEY_HELP << " \t\t\t Show this help message" << std::endl;

    std::cout << "\nProtocol Selection:" << std::endl;
    std::cout << "  -" << KEY_DISTANCE_COMPUTATION
              << " <protocol> \t Run the distance computation protocol (" << VAL_DISTANCE_COMPUTATION_BATCHED << "/" << VAL_DISTANCE_COMPUTATION_INDIVIDUAL << "/" << VAL_DISTANCE_COMPUTATION_SHARED_BATCHED << ")" << std::endl;
    std::cout << "  -" << KEY_THRESHOLD_COMPARISON
              << " <protocol> \t Run the threshold comparison protocol (" << VAL_THRESHOLD_COMPARISON_NP << ")" << std::endl;
    std::cout << "  -" << KEY_SECRET_SHARED
              << " \t\t Operate on secret-shared inputs and outputs (default: false)" << std::endl;

    std::cout << "\nProtocol Parameters:" << std::endl;
    std::cout << "  -" << KEY_QUERIER_SIZE << " <size> \t\t Set querier size (default: " << DEFAULT_QUERIER_SIZE << ")" << std::endl;
    std::cout << "  -" << KEY_RESPONDER_SIZE << " <size> \t Set responder size (default: " << DEFAULT_RESPONDER_SIZE << ")" << std::endl;
    std::cout << "  -" << KEY_VECTOR_SIZE << " <size> \t\t Set vector size (default: " << DEFAULT_VECTOR_SIZE << ")" << std::endl;
    std::cout << "  -" << KEY_THRESHOLD << " <value> \t\t Set threshold (default: " << DEFAULT_THRESHOLD << ")" << std::endl;

    std::cout << "\nExecution Parameters:" << std::endl;
    std::cout << "  -" << KEY_RUN_SENDER
              << " \t\t\t Run only sender" << std::endl;
    std::cout << "  -" << KEY_RUN_RECEIVER
              << " \t\t Run only receiver" << std::endl;
    std::cout << "  -" << KEY_IP 
              << " <ip:port> \t Set IP address and port (default: " << DEFAULT_IP << ")" << std::endl;

    std::cout << "\nPrimitives:" << std::endl;
    std::cout << "  -" << KEY_OT
              << " <protocol> \t\t Set 1-out-of-2 OT protocol (" << VAL_OT_IKNP << "/" << VAL_OT_SOFTSPOKEN << "/" << VAL_OT_SILENT << ", default: " << DEFAULT_OT << ")" << std::endl;
    std::cout << "  -" << KEY_PRF
              << " <prf> \t\t\t Set PRF (" << VAL_PRF_SHA256 << "/" << VAL_PRF_BLAKE3 << "/" << VAL_PRF_AES_CTR << ", default: " << DEFAULT_PRF << ")" << std::endl;
}

inline config parseConfig(osuCrypto::CLP &cmd)
{
    config cfg;

    // Ensure no unknown keys are present
    for (const auto &[key, val] : cmd.mKeyValues)
    {
        if (key != KEY_RESPONDER_SIZE && key != KEY_QUERIER_SIZE && key != KEY_VECTOR_SIZE &&
            key != KEY_THRESHOLD && key != KEY_IP && key != KEY_DISTANCE_COMPUTATION &&
            key != KEY_THRESHOLD_COMPARISON && key != KEY_RUN_SENDER && key != KEY_RUN_RECEIVER &&
            key != KEY_OT && key != KEY_PRF && key != KEY_SECRET_SHARED && key != KEY_HELP)
        {
            std::cerr << "Unknown option: " << key << std::endl;
            exit(1);
        }
    }

    cfg.querier_size = cmd.getOr<size_t>(KEY_QUERIER_SIZE, DEFAULT_QUERIER_SIZE);
    cfg.responder_size = cmd.getOr<size_t>(KEY_RESPONDER_SIZE, DEFAULT_RESPONDER_SIZE);
    cfg.vector_size = cmd.getOr<size_t>(KEY_VECTOR_SIZE, DEFAULT_VECTOR_SIZE);
    cfg.modulus = cfg.vector_size + 1;
    cfg.p_bits = std::ceil(std::log2(cfg.modulus));
    cfg.threshold_comparison_count = cfg.querier_size * cfg.responder_size;
    cfg.threshold = cmd.getOr<size_t>(KEY_THRESHOLD, DEFAULT_THRESHOLD);

    assert(std::pow(2, cfg.p_bits) >= cfg.modulus);
    assert(cfg.modulus > 0 && (uint64_t)cfg.modulus > cfg.vector_size);
    assert(cfg.threshold <= cfg.vector_size);

    cfg.run_sender = cmd.isSet(KEY_RUN_SENDER);
    cfg.run_receiver = cmd.isSet(KEY_RUN_RECEIVER);
    cfg.ip = cmd.getOr<std::string>(KEY_IP, DEFAULT_IP);

    cfg.prf = cmd.getOr<std::string>(KEY_PRF, DEFAULT_PRF);
    assert(cfg.prf == VAL_PRF_SHA256 || cfg.prf == VAL_PRF_BLAKE3 || cfg.prf == VAL_PRF_AES_CTR);
    cfg.ot = cmd.getOr<std::string>(KEY_OT, DEFAULT_OT);
    assert(cfg.ot == VAL_OT_IKNP || cfg.ot == VAL_OT_SOFTSPOKEN || cfg.ot == VAL_OT_SILENT);

    if (cmd.hasValue(KEY_DISTANCE_COMPUTATION))
    {
        std::string distance_computation = cmd.get<std::string>(KEY_DISTANCE_COMPUTATION);
        assert(distance_computation == VAL_DISTANCE_COMPUTATION_BATCHED || distance_computation == VAL_DISTANCE_COMPUTATION_INDIVIDUAL || distance_computation == VAL_DISTANCE_COMPUTATION_SHARED_BATCHED);
        cfg.distance_computation = distance_computation;
    }
    else
    {
        cfg.distance_computation = std::nullopt;
    }

    if (cmd.hasValue(KEY_THRESHOLD_COMPARISON))
    {
        std::string threshold_comparison = cmd.get<std::string>(KEY_THRESHOLD_COMPARISON);
        assert(threshold_comparison == VAL_THRESHOLD_COMPARISON_NP);
        cfg.threshold_comparison = threshold_comparison;
    }
    else
    {
        cfg.threshold_comparison = std::nullopt;
    }

    cfg.secret_shared = cmd.isSet(KEY_SECRET_SHARED);

    if (cmd.isSet(KEY_HELP) || (!cfg.threshold_comparison.has_value() && !cfg.distance_computation.has_value()))
    {
        print_help();
        exit(0);
    }

    return cfg;
}
