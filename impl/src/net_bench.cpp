// Two-process benchmark over TCP: run one party per process.
//   net_bench -role 0 (sender) | 1 (receiver) -addr 127.0.0.1:1300 [same options as fpsi_bench]
// Both processes generate the same synthetic data from -seed (or read -data) and use only their
// own half. The receiver checks its output against an exact plaintext index (-indexverify).
#include "fpsi.h"

#define COPROTO_ENABLE_BOOST
#include <coproto/Socket/AsioSocket.h>
#include <cryptoTools/Common/CLP.h>
#include <macoro/sync_wait.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <sys/resource.h>
#include <unordered_map>

using namespace mip;

static double cpuNow()
{
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6;
}

int main(int argc, char** argv)
{
    oc::CLP cmd(argc, argv);
    FpsiConfig cfg;
    cfg.d = cmd.getOr("d", 128ull);
    cfg.delta = cmd.getOr("delta", 4ull);
    cfg.b = cmd.getOr("b", cfg.delta + 1);
    cfg.s = cmd.getOr("s", 1ull);
    cfg.coverQ = cmd.getOr("cover", 0ull);
    cfg.Bbar = cmd.getOr("Bbar", 0ull);
    cfg.Ubar = cmd.getOr("Ubar", 0ull);
    cfg.pow2Leak = cmd.isSet("pow2");
    cfg.noU = cmd.isSet("noU");    cfg.dpEps = cmd.getOr("dpeps", 0.0);
    cfg.dpDelta = cmd.getOr("dpdelta", cfg.dpDelta);
    cfg.allowFallback = !cmd.isSet("nofallback");
    cfg.noCompress = cmd.isSet("nocompress");
    cfg.uniqOnly = cmd.isSet("uniqc");
    cfg.keyCheck = cmd.isSet("keycheck");
    cfg.leakOnly = cmd.isSet("leakonly");
    u64 role = cmd.getOr("role", 0ull);
    std::string addr = cmd.getOr<std::string>("addr", "127.0.0.1:1300");
    u64 m = cmd.getOr("m", 1024ull), n = cmd.getOr("n", 1024ull), db = cfg.d / 8;
    oc::PRNG prng(oc::block(cmd.getOr("seed", 1ull), 99));

    ItemMatrix X, Y;
    if (cmd.isSet("data"))
    {
        std::ifstream in(cmd.get<std::string>("data"), std::ios::binary);
        u64 dd;
        in.read((char*)&m, 8); in.read((char*)&n, 8); in.read((char*)&dd, 8);
        if (!in || dd != cfg.d)
            throw std::runtime_error("data file header does not match -d");
        X.resize(m, db); Y.resize(n, db);
        in.read((char*)X.data(), m * db);
        in.read((char*)Y.data(), n * db);
        if (!in || in.peek() != std::ifstream::traits_type::eof())
            throw std::runtime_error("data file length does not match its header");
    }
    else
    {
        X.resize(m, db); Y.resize(n, db);
        prng.get(X.data(), X.size());
        prng.get(Y.data(), Y.size());
        u64 plant = std::min(m, n) / 8;
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

    auto sock = coproto::asioConnect(addr, role == 1);
    oc::PRNG pp(oc::block(role + 7, cmd.getOr("seed", 1ull)) ^ oc::sysRandomSeed());
    FpsiInfo info;
    ItemMatrix out;
    double c0 = cpuNow();
    auto t0 = std::chrono::steady_clock::now();
    if (role == 0)
        macoro::sync_wait(fpsiSender(cfg, X, sock, pp, info));
    else
        macoro::sync_wait(fpsiReceiver(cfg, Y, sock, pp, out, info));
    macoro::sync_wait(sock.flush());
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double cpu = cpuNow() - c0;
    std::cout << "role=" << role << " m=" << m << " n=" << n << " d=" << cfg.d << " delta=" << cfg.delta
              << " B=" << info.B << " U=" << info.U << " Q=" << info.Q << " fallback=" << info.fellBack
              << "\nwall_s=" << wall << " cpu_s=" << cpu << " sent_MB=" << sock.bytesSent() / double(1 << 20)
              << " recv_MB=" << sock.bytesReceived() / double(1 << 20) << "\n";

    if (role == 1 && cmd.isSet("indexverify"))
    {
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
        std::set<std::vector<std::uint8_t>> truth, got;
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
            if (hit) truth.insert(std::vector<std::uint8_t>(&X(i, 0), &X(i, 0) + db));
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
