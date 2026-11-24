#include "distance_computation/distance_computation.h"
#include "threshold_comparison/threshold_comparison.h"
#include "config.h"
#include "libOTe/TwoChooseOne/OTExtInterface.h"
#include <cryptoTools/Common/CLP.h>

#include "coproto/Socket/AsioSocket.h"
#include "cryptoTools/Common/BitVector.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "cryptoTools/Common/Timer.h"
#include "cryptoTools/Common/block.h"
#include <bitset>
#include "cli.h"
#include "matched.h"

int main(int argc, char **argv)
{

    osuCrypto::CLP cmd = osuCrypto::CLP(argc, argv);

    config cfg = parseConfig(cmd);
    print_config(cfg);
    if (std::getenv("OTFPSI_MATCHED_DATA")) // added: output-matched stage 1 on real inputs
        return runMatchedStage1(cfg);
    if (std::getenv("OTFPSI_REAL_DATA")) // added (e3): native pairwise-output otFPSI on real inputs
        return runRealNative(cfg);

    double total_time = 0;
    size_t total_communication = 0;

    if (cfg.distance_computation.has_value())
    {
        Timer timer;
        std::cout << "Hamming Distance Protocol" << std::endl;
        runDistanceComputationProtocol(cfg, timer, total_communication);
        total_time += (timer["Interleaved Distance Computation"] - timer["start"]).count() / 1000.0 / 1000.0;
        std::cout << timer << std::endl;
    }

    if (cfg.threshold_comparison.has_value())
    {
        Timer timer;
        std::cout << "Threshold Comparison Protocol" << std::endl;
        runThresholdComparisonProtocol(cfg, timer, total_communication);
        total_time += (timer["Interleaved Threshold Comparison"] - timer["start"]).count() / 1000.0 / 1000.0;
        std::cout << timer << std::endl;
    }

    std::cout << std::endl << std::endl << "Total Time: " << total_time << " ms. " << total_time / 1000.0 << " s" << std::endl;
    std::cout << "Total Communication: " << total_communication << " bytes. " << total_communication / 1024.0 << " KB. " << total_communication / (1024.0 * 1024.0) << " MB. " << total_communication / (1024.0 * 1024.0 * 1024.0) << " GB." << std::endl;

    return 0;
}
