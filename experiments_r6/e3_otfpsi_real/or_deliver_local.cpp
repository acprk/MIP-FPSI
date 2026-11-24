// e3: in-process variant of impl/src/or_deliver.cpp (stage 2 of the output-matched otFPSI baseline:
// OR over each sender vector's n shared comparison bits (GMW) + OT delivery, exactly as in our
// protocol, via mip::orDeliverSender / mip::orDeliverReceiver from libmipfpsi.a).
// Loopback TCP is unavailable in the measurement sandbox, so both parties run as two threads of one
// process connected by coproto::LocalAsyncSocket; CPU is reported for the whole process (both parties).
// The sender's shuffle of X is applied in stage 1 (otfpsi with OTFPSI_MATCHED_SHUFFLED_DATA), so -data
// must be the permuted data file written there; the receiver checks its output set against the
// plaintext fuzzy intersection, and additionally that the delivered order equals the permuted order.
//   or_deliver_local -data f.bin -shareS f.S -shareR f.R -d 64 -delta 4
#include "fpsi.h"
#include "mip.h"

#include <coproto/Socket/LocalAsyncSock.h>
#include <cryptoTools/Common/BitVector.h>
#include <cryptoTools/Common/CLP.h>
#include <macoro/sync_wait.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
#include <sys/resource.h>

using namespace mip;

static double cpuNow()
{
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return u.ru_utime.tv_sec + u.ru_stime.tv_sec + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
}

static oc::BitVector readShare(const std::string& p, u64 m, u64 n)
{
    std::ifstream sf(p, std::ios::binary);
    u64 sm, sn;
    sf.read((char*)&sm, 8); sf.read((char*)&sn, 8);
    if (!sf || sm != m || sn != n) throw std::runtime_error("share file does not match data: " + p);
    oc::BitVector share(m * n);
    sf.read((char*)share.data(), share.sizeBytes());
    if (!sf) throw std::runtime_error("share file truncated: " + p);
    return share;
}

int main(int argc, char** argv)
{
    oc::CLP cmd(argc, argv);
    FpsiConfig cfg;
    cfg.d = cmd.getOr("d", 64ull);
    cfg.delta = cmd.getOr("delta", 4ull);
    u64 db = cfg.d / 8;
    std::ifstream in(cmd.get<std::string>("data"), std::ios::binary);
    u64 m, n, dd;
    in.read((char*)&m, 8); in.read((char*)&n, 8); in.read((char*)&dd, 8);
    if (!in || dd != cfg.d) throw std::runtime_error("data header does not match -d");
    ItemMatrix X(m, db), Y(n, db);
    in.read((char*)X.data(), m * db);
    in.read((char*)Y.data(), n * db);
    auto shareS = readShare(cmd.get<std::string>("shareS"), m, n);
    auto shareR = readShare(cmd.get<std::string>("shareR"), m, n);

    auto socks = coproto::LocalAsyncSocket::makePair();
    oc::PRNG prngS(oc::sysRandomSeed()), prngR(oc::sysRandomSeed());
    ItemMatrix out;
    double c0 = cpuNow();
    auto t0 = std::chrono::steady_clock::now();
    std::exception_ptr err;
    std::thread recvThread([&] {
        try
        {
            macoro::sync_wait(orDeliverReceiver(cfg, m, n, shareR, socks[1], prngR, out));
            macoro::sync_wait(socks[1].flush());
        }
        catch (...) { err = std::current_exception(); }
    });
    macoro::sync_wait(orDeliverSender(cfg, X, shareS, n, socks[0], prngS));
    macoro::sync_wait(socks[0].flush());
    recvThread.join();
    if (err) std::rethrow_exception(err);
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "m=" << m << " n=" << n << "\nwall_s=" << wall << " cpu_s=" << cpuNow() - c0
              << " sent_MB=" << socks[0].bytesSent() / double(1 << 20)
              << " recv_MB=" << socks[0].bytesReceived() / double(1 << 20) << "\n";

    // check (outside the timed section)
    std::set<std::vector<std::uint8_t>> truth, got;
    std::vector<std::vector<std::uint8_t>> truthOrder;
    for (u64 i = 0; i < m; ++i)
        for (u64 j = 0; j < n; ++j)
            if (hamming(&X(i, 0), &Y(j, 0), cfg.d) <= cfg.delta)
            {
                truth.insert(std::vector<std::uint8_t>(&X(i, 0), &X(i, 0) + db));
                truthOrder.emplace_back(&X(i, 0), &X(i, 0) + db);
                break;
            }
    bool sameOrder = out.rows() == truthOrder.size();
    for (u64 i = 0; i < out.rows(); ++i)
    {
        std::vector<std::uint8_t> v(&out(i, 0), &out(i, 0) + db);
        got.insert(v);
        if (sameOrder && v != truthOrder[i]) sameOrder = false;
    }
    u64 fn = 0, fp = 0;
    for (auto& v : truth) fn += !got.count(v);
    for (auto& v : got) fp += !truth.count(v);
    std::cout << "matches=" << got.size() << " truth=" << truth.size() << " false_neg=" << fn
              << " false_pos=" << fp << " delivered_in_permuted_order=" << sameOrder << "\n";
    return (fn || fp) ? 2 : 0;
}
