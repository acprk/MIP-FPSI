#pragma once

#define VAL_OT_IKNP "iknp"
#define VAL_OT_SOFTSPOKEN "softspoken"
#define VAL_OT_SILENT "silent"

#define VAL_DISTANCE_COMPUTATION_BATCHED "batched"
#define VAL_DISTANCE_COMPUTATION_INDIVIDUAL "individual"
#define VAL_DISTANCE_COMPUTATION_SHARED_BATCHED "shared_batched"

#define VAL_THRESHOLD_COMPARISON_NP "np"

#define VAL_PRF_SHA256 "sha256"
#define VAL_PRF_BLAKE3 "blake3"
#define VAL_PRF_AES_CTR "aes"

#define DEFAULT_VECTOR_SIZE 511
#define DEFAULT_QUERIER_SIZE 1
#define DEFAULT_RESPONDER_SIZE 131072
#define DEFAULT_THRESHOLD 42 // Does not impact performance
#define DEFAULT_IP "localhost:1212"
#define DEFAULT_PRF VAL_PRF_AES_CTR
#define DEFAULT_OT VAL_OT_SOFTSPOKEN

struct config
{
    size_t vector_size;
    size_t p_bits; // Number of bits of modulus p  (which is set to p=vector_size + 1)
    int64_t modulus; // DANGER ZONE: The modulus is explicitly signed because [signed] % [unsigned] may not behave as expected
                     // when the first operand is negative and the second operand is not a power of two. [signed] % [signed]
                     // behaves as expected.
    size_t threshold_comparison_count; // Total number of threshold comparisons to perform per iteration
    size_t threshold; 
    bool run_sender;
    bool run_receiver;
    std::string ip;
    std::optional<std::string> distance_computation;
    std::optional<std::string> threshold_comparison;
    std::string prf;
    std::string ot;

    size_t querier_size;
    size_t responder_size;
    bool secret_shared;
};

#define MOD(x, m) ((x) < 0 ? (((x) % (m)) + (m)) % (m) : (x) % (m))

inline void print_config(const config &cfg)
{
    std::cout << "----------------------------------------" << std::endl;

    std::cout << "Configuration:" << std::endl;
    std::cout << "  Distance Computation Protocol: " << cfg.distance_computation.value_or("Skipped") << std::endl;
    std::cout << "  Threshold Comparison Protocol: " << cfg.threshold_comparison.value_or("Skipped") << std::endl;
    std::cout << "  Querier Size: " << cfg.querier_size << std::endl;
    std::cout << "  Responder Size: " << cfg.responder_size << std::endl;
    std::cout << "  Vector Size: " << cfg.vector_size << std::endl;
    std::cout << "  P Bits: " << cfg.p_bits << std::endl;
    std::cout << "  Modulus: " << cfg.modulus << std::endl;
    std::cout << "  Threshold Comparison Count: " << cfg.threshold_comparison_count << std::endl;
    std::cout << "  Threshold: " << cfg.threshold << std::endl;
    std::cout << "  Run Sender: " << (cfg.run_sender ? "Yes" : "No") << std::endl;
    std::cout << "  Run Receiver: " << (cfg.run_receiver ? "Yes" : "No") << std::endl;
    std::cout << "  IP: " << cfg.ip << std::endl;
    std::cout << "  PRF: " << cfg.prf << std::endl;
    std::cout << "  OT Protocol: " << cfg.ot << std::endl;
    std::cout << "  Secret Shared: " << (cfg.secret_shared ? "Yes" : "No") << std::endl;
    std::cout << "----------------------------------------" << std::endl;
}