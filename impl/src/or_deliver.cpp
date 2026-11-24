// Stage 2 of the output-matched otFPSI baseline: OR over each sender vector's n shared comparison bits
// (GMW) and OT delivery, exactly as in our protocol. Inputs: the data file (u64 m, n, d; X rows; Y rows)
// and this party's share file from otFPSI stage 1 (u64 m, n; m*n bits, row-major by sender vector).
//   or_deliver -role 0|1 -addr host:port -data f.bin -share f.S|f.R -d 128 -delta 4
// The receiver checks its output against the plaintext fuzzy intersection.
#include "fpsi.h"
#include "mip.h"

#include <coproto/Socket/AsioSocket.h>
#include <cryptoTools/Common/BitVector.h>
#include <cryptoTools/Common/CLP.h>
#include <macoro/sync_wait.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <sys/resource.h>

using namespace mip;

static double cpuNow()
{
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return u.ru_utime.tv_sec + u.ru_stime.tv_sec + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
}

int main(int argc, char** argv)
{
    oc::CLP cmd(argc, argv);
    FpsiConfig cfg;
    cfg.d = cmd.getOr("d", 128ull);
    cfg.delta = cmd.getOr("delta", 4ull);
    u64 role = cmd.getOr("role", 0ull), db = cfg.d / 8;
    std::ifstream in(cmd.get<std::string>("data"), std::ios::binary);
    u64 m, n, dd;
    in.read((char*)&m, 8); in.read((char*)&n, 8); in.read((char*)&dd, 8);
    if (!in || dd != cfg.d) throw std::runtime_error("data header does not match -d");
    ItemMatrix X(m, db), Y(n, db);
    in.read((char*)X.data(), m * db);
    in.read((char*)Y.data(), n * db);
    std::ifstream sf(cmd.get<std::string>("share"), std::ios::binary);
    u64 sm, sn;
    sf.read((char*)&sm, 8); sf.read((char*)&sn, 8);
    if (sm != m || sn != n) throw std::runtime_error("share file does not match data");
    oc::BitVector share(m * n);
    sf.read((char*)share.data(), share.sizeBytes());

    auto sock = coproto::asioConnect(cmd.getOr<std::string>("addr", "127.0.0.1:1400"), role == 1);
    oc::PRNG prng(oc::sysRandomSeed());
    ItemMatrix out;
    double c0 = cpuNow();
    auto t0 = std::chrono::steady_clock::now();
    if (role == 0)
        macoro::sync_wait(orDeliverSender(cfg, X, share, n, sock, prng));
    else
        macoro::sync_wait(orDeliverReceiver(cfg, m, n, share, sock, prng, out));
    macoro::sync_wait(sock.flush());
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "role=" << role << " m=" << m << " n=" << n << "\nwall_s=" << wall << " cpu_s=" << cpuNow() - c0
              << " sent_MB=" << sock.bytesSent() / double(1 << 20) << " recv_MB=" << sock.bytesReceived() / double(1 << 20) << "\n";
    if (role == 1)
    {
        std::set<std::vector<std::uint8_t>> truth, got;
        for (u64 i = 0; i < m; ++i)
            for (u64 j = 0; j < n; ++j)
                if (hamming(&X(i, 0), &Y(j, 0), cfg.d) <= cfg.delta)
                {
                    truth.insert(std::vector<std::uint8_t>(&X(i, 0), &X(i, 0) + db));
                    break;
                }
        for (u64 i = 0; i < out.rows(); ++i)
            got.insert(std::vector<std::uint8_t>(&out(i, 0), &out(i, 0) + db));
        u64 fn = 0, fp = 0;
        for (auto& v : truth) fn += !got.count(v);
        for (auto& v : got) fp += !truth.count(v);
        std::cout << "matches=" << got.size() << " truth=" << truth.size() << " false_neg=" << fn
                  << " false_pos=" << fp << "\n";
        return (fn || fp) ? 2 : 0;
    }
    return 0;
}
