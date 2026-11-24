// Benchmark / end-to-end correctness harness (both parties in one process, in-memory sockets).
//   fpsi_bench -m 1024 -n 1024 -d 128 -delta 4 -b 5 -s 1 -nt 1 [-data file.bin] [-plant 128]
// Data file format: u64 m, u64 n, u64 d, then m rows X and n rows Y of d/8 bytes.
#include "fpsi.h"

#include <coproto/Socket/LocalAsyncSock.h>
#include <cryptoTools/Common/CLP.h>
#include <macoro/sync_wait.h>
#include <macoro/when_all.h>

#include <chrono>
#include <sys/resource.h>
#include <fstream>
#include <iostream>
#include <set>
#include <unordered_map>
#include <string>

using namespace mip;

int main(int argc, char** argv)
{
    oc::CLP cmd(argc, argv);
    FpsiConfig cfg;
    cfg.d = cmd.getOr("d", 128ull);
    cfg.delta = cmd.getOr("delta", 4ull);
    cfg.b = cmd.getOr("b", cfg.delta + 1);
    cfg.s = cmd.getOr("s", 1ull);
    cfg.threads = cmd.getOr("nt", 1ull);
    cfg.Bbar = cmd.getOr("Bbar", 0ull);
    cfg.coverQ = cmd.getOr("cover", 0ull);
    cfg.noCompress = cmd.isSet("nocompress");
    cfg.keyCheck = cmd.isSet("keycheck"); // experiments_r6: only change vs impl/src/main.cpp
    cfg.uniqOnly = cmd.isSet("uniqc");
    cfg.Ubar = cmd.getOr("Ubar", 0ull);
    cfg.pow2Leak = cmd.isSet("pow2");
    cfg.noU = cmd.isSet("noU");    cfg.dpEps = cmd.getOr("dpeps", 0.0);
    cfg.dpDelta = cmd.getOr("dpdelta", cfg.dpDelta);
    cfg.allowFallback = !cmd.isSet("nofallback");
    cfg.gmwThreshold = !cmd.isSet("otthreshold");
    u64 m = cmd.getOr("m", 1024ull), n = cmd.getOr("n", 1024ull);
    u64 db = cfg.d / 8;
    oc::PRNG prng(oc::block(cmd.getOr("seed", 1ull), 99));

    ItemMatrix X, Y;
    if (cmd.isSet("data"))
    {
        std::ifstream in(cmd.get<std::string>("data"), std::ios::binary);
        u64 dd;
        in.read((char*)&m, 8); in.read((char*)&n, 8); in.read((char*)&dd, 8);
        if (dd != cfg.d) { std::cerr << "d mismatch\n"; return 1; }
        X.resize(m, db); Y.resize(n, db);
        in.read((char*)X.data(), m * db);
        in.read((char*)Y.data(), n * db);
    }
    else
    {
        X.resize(m, db); Y.resize(n, db);
        prng.get(X.data(), X.size());
        prng.get(Y.data(), Y.size());
        u64 plant = cmd.getOr("plant", std::min(m, n) / 8);
        // planted pairs: distance in [0, delta+2] (some just above the threshold)
        for (u64 t = 0; t < plant; ++t)
        {
            u64 i = prng.get<u64>() % m, j = prng.get<u64>() % n;
            std::memcpy(&Y(j, 0), &X(i, 0), db);
            u64 w = prng.get<u64>() % (cfg.delta + 3);
            for (u64 e = 0; e < w; ++e)
            {
                u64 bit = prng.get<u64>() % cfg.d;
                Y(j, bit / 8) ^= std::uint8_t(1u << (bit % 8));
            }
        }
    }

    // brute-force ground truth
    std::set<std::vector<std::uint8_t>> truth;
    if (cmd.isSet("indexverify"))
    {
        // exact plaintext search via a pigeonhole index on delta+1 contiguous blocks
        // (independent of the protocol's permuted tables): H<=delta => some block equal.
        u64 nb = cfg.delta + 1;
        std::vector<std::unordered_map<std::string, std::vector<u64>>> idx(nb);
        auto blk = [&](const std::uint8_t* row, u64 t) {
            std::string s;
            for (u64 k = t * cfg.d / nb; k < (t + 1) * cfg.d / nb; ++k)
                s.push_back(char(getBit(row, k)));
            return s;
        };
        for (u64 j = 0; j < n; ++j)
            for (u64 t = 0; t < nb; ++t)
                idx[t][blk(&Y(j, 0), t)].push_back(j);
        for (u64 i = 0; i < m; ++i)
        {
            bool hit = false;
            for (u64 t = 0; t < nb && !hit; ++t)
            {
                auto it = idx[t].find(blk(&X(i, 0), t));
                if (it == idx[t].end()) continue;
                for (auto j : it->second)
                    if (hamming(&X(i, 0), &Y(j, 0), cfg.d) <= cfg.delta) { hit = true; break; }
            }
            if (hit)
                truth.insert(std::vector<std::uint8_t>(&X(i, 0), &X(i, 0) + db));
        }
    }
    else if (!cmd.isSet("noverify"))
        for (u64 i = 0; i < m; ++i)
            for (u64 j = 0; j < n; ++j)
                if (hamming(&X(i, 0), &Y(j, 0), cfg.d) <= cfg.delta)
                {
                    truth.insert(std::vector<std::uint8_t>(&X(i, 0), &X(i, 0) + db));
                    break;
                }

    auto socks = coproto::LocalAsyncSocket::makePair();
    oc::PRNG ps(prng.get()), pr(prng.get());
    FpsiInfo is, ir;
    ItemMatrix out;
    oc::Timer ts, tr;
    ts.setTimePoint("begin");
    tr.setTimePoint("begin");
    rusage ru0;
    getrusage(RUSAGE_SELF, &ru0);
    double cpu0 = ru0.ru_utime.tv_sec + ru0.ru_utime.tv_usec * 1e-6 + ru0.ru_stime.tv_sec + ru0.ru_stime.tv_usec * 1e-6;
    auto t0 = std::chrono::steady_clock::now();
    if (cmd.isSet("allpairs"))
    {
        auto r = macoro::sync_wait(macoro::when_all_ready(
            allPairsSender(cfg, X, socks[0], ps, &ts),
            allPairsReceiver(cfg, Y, socks[1], pr, out, &tr)));
        std::get<0>(r).result();
        std::get<1>(r).result();
    }
    else
    {
        auto r = macoro::sync_wait(macoro::when_all_ready(
            fpsiSender(cfg, X, socks[0], ps, is, &ts),
            fpsiReceiver(cfg, Y, socks[1], pr, out, ir, &tr)));
        std::get<0>(r).result();
        std::get<1>(r).result();
    }
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    double cpu = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6 - cpu0;

    std::set<std::vector<std::uint8_t>> got;
    for (u64 i = 0; i < out.rows(); ++i)
        got.insert(std::vector<std::uint8_t>(&out(i, 0), &out(i, 0) + db));
    u64 fn = 0, fp = 0;
    for (auto& v : truth) fn += !got.count(v);
    if (!cmd.isSet("noverify"))
        for (auto& v : got) fp += !truth.count(v);

    double mb = (socks[0].bytesSent() + socks[1].bytesSent()) / double(1 << 20);
    std::cout << "m=" << m << " n=" << n << " d=" << cfg.d << " delta=" << cfg.delta
              << " b=" << cfg.b << " s=" << cfg.s << " r=" << is.r << " T=" << is.T
              << " B=" << is.B << " U=" << is.U << " Q=" << is.Q << " bins=" << is.bins
              << " prog=" << ir.programmed << " tau=" << is.tau << " fallback=" << is.fellBack << " nt=" << cfg.threads << "\n"
              << "time_s=" << secs << " cpu_s(both parties)=" << cpu << " comm_MB=" << mb
              << " (S->R " << socks[0].bytesSent() / double(1 << 20)
              << ", R->S " << socks[1].bytesSent() / double(1 << 20) << ")\n"
              << "matches=" << got.size() << " truth=" << truth.size()
              << " false_neg=" << fn << " false_pos=" << fp << "\n";
    if (cmd.isSet("v"))
        std::cout << "sender\n" << ts << "\nreceiver\n" << tr << std::endl;
    return (fn || fp) ? 2 : 0;
}
