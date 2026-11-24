// Micro-benchmark of the threshold circuit under volePSI GMW.
//   gmw_bench -N 100000 -d 128 -delta 4 -nt 1
#include "circuits.h"
#include "volePSI/GMW/Gmw.h"

#include <coproto/Socket/LocalAsyncSock.h>
#include <cryptoTools/Common/CLP.h>
#include <macoro/sync_wait.h>
#include <macoro/when_all.h>

#include <chrono>
#include <iostream>

using namespace mip;

int main(int argc, char** argv)
{
    oc::CLP cmd(argc, argv);
    u64 N = cmd.getOr("N", 100000ull), d = cmd.getOr("d", 128ull), delta = cmd.getOr("delta", 4ull);
    u64 nt = cmd.getOr("nt", 1ull);
    u64 tau = cmd.getOr("tau", 0ull);
    // -or: OR over d input bits (aggregation cost of an output-matched all-pairs baseline)
    bool orMode = cmd.isSet("or");
    auto cir = orMode ? orCircuit(d) : (cmd.isSet("iz") ? volePSI::isZeroCircuit(d + tau) : thresholdCircuit(d, delta, tau));
    cir.levelByAndDepth();
    std::cout << "gates=" << cir.mGates.size() << " and=" << cir.mNonlinearGateCount
              << " levels=" << cir.mLevelCounts.size() << "\n";

    oc::PRNG prng(oc::block(1, 1));
    oc::Matrix<std::uint8_t> a0(N, oc::divCeil(d + tau, 8)), a1(N, oc::divCeil(d + tau, 8));
    prng.get(a0.data(), a0.size());
    prng.get(a1.data(), a1.size());
    // half of the rows are near: a1 = a0 ^ (sparse vector of weight <= delta+1), zero tag
    for (u64 i = 0; i < N; i += 2)
    {
        std::memcpy(&a1(i, 0), &a0(i, 0), a0.cols());
        u64 w = prng.get<u64>() % (delta + 2);
        for (u64 e = 0; e < w; ++e)
        {
            u64 k = prng.get<u64>() % d;
            a1(i, k / 8) ^= std::uint8_t(1u << (k % 8));
        }
    }

    volePSI::Gmw g0, g1;
    g0.init(N, cir, nt, 0, prng.get());
    g1.init(N, cir, nt, 1, prng.get());
    g0.setInput(0, oc::MatrixView<std::uint8_t>(a0));
    g1.setInput(0, oc::MatrixView<std::uint8_t>(a1));
    oc::Timer timer;
    timer.setTimePoint("begin");
    g0.setTimer(timer);
    auto socks = coproto::LocalAsyncSocket::makePair();
    auto t0 = std::chrono::steady_clock::now();
    auto r = macoro::sync_wait(macoro::when_all_ready(g0.run(socks[0]), g1.run(socks[1])));
    std::get<0>(r).result();
    std::get<1>(r).result();
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    {
        auto o0 = g0.getOutputView(0), o1 = g1.getOutputView(0);
        u64 ones = 0, wrong = 0;
        for (u64 i = 0; i < N; ++i)
        {
            bool bit = ((o0(0, i / 8) ^ o1(0, i / 8)) >> (i % 8)) & 1;
            u64 hw = 0, tagnz = 0;
            for (u64 k = 0; k < d; ++k)
                hw += ((a0(i, k / 8) ^ a1(i, k / 8)) >> (k % 8)) & 1;
            for (u64 k = d; k < d + tau; ++k)
                tagnz |= ((a0(i, k / 8) ^ a1(i, k / 8)) >> (k % 8)) & 1;
            bool exp = orMode ? hw > 0 : (hw <= delta && !tagnz);
            ones += bit;
            wrong += bit != exp;
        }
        std::cout << "output ones=" << ones << " wrong=" << wrong << "\n";
    }
    std::cout << "N=" << N << " nt=" << nt << " time_s=" << s
              << " comm_MB=" << (socks[0].bytesSent() + socks[1].bytesSent()) / double(1 << 20)
              << " ANDs/s=" << N * cir.mNonlinearGateCount / s << "\n" << timer << "\n";
    return 0;
}
