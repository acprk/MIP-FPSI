// Receiver-side Hamming-ball expansion + plain PSI (RR22 / volePSI RsPsi, UNMODIFIED upstream).
//
// Fuzzy PSI target: receiver R (holds Y, n vectors of d bits) learns
//   I = { x in X : exists y in Y, HD(x,y) <= delta },   sender S holds X (m vectors).
// Baseline: R expands every y into its radius-delta Hamming ball, takes the union, pads with random
// dummies to exactly n*V(d,delta) items, and runs volePSI RsPsi with R = RsPsiReceiver (the party
// that learns the intersection; it holds the big set) and S = RsPsiSender (|X| = m, learns nothing).
//
// Two processes over loopback TCP:
//   ballexp_bench -role 0 (sender S) | 1 (receiver R) -addr 127.0.0.1:1400 -m M -n N -d D -delta T
//     -mode synth : random items of the right sizes (cost is data-independent given (m, n*V)).
//                   -nr overrides the receiver set size; -plant P puts P common items.
//     -mode real  : real d-bit vectors, real ball expansion, dedup, padding, brute-force check (R).
//   -seed s : shared data seed (both processes must use the same).  -nt threads (default 1).
#define COPROTO_ENABLE_BOOST
#include <coproto/Socket/AsioSocket.h>
#include <cryptoTools/Common/CLP.h>
#include <cryptoTools/Crypto/AES.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <macoro/sync_wait.h>
#include "volePSI/RsPsi.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <sys/resource.h>
#include <vector>

using namespace volePSI;
using oc::block;

static double cpuNow()
{
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6;
}
static double peakRssMiB()
{
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss / 1024.0; // ru_maxrss is KiB on Linux
}
static double nowS()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static u64 binom(u64 n, u64 k)
{
    long double r = 1;
    for (u64 i = 1; i <= k; ++i) r = r * (n - k + i) / i;
    return (u64)(r + 0.5L);
}
static u64 ballVol(u64 d, u64 t)
{
    u64 v = 0;
    for (u64 i = 0; i <= t; ++i) v += binom(d, i);
    return v;
}

// d-bit vector (d <= 128) stored in a block: bit k of the vector = bit k of the 128-bit word.
static inline block flipBit(block b, u64 k)
{
    u64 w[2];
    std::memcpy(w, &b, 16);
    w[k / 64] ^= (1ull << (k % 64));
    std::memcpy(&b, w, 16);
    return b;
}
static inline u64 hd(block a, block b)
{
    u64 x[2], y[2];
    std::memcpy(x, &a, 16);
    std::memcpy(y, &b, 16);
    return __builtin_popcountll(x[0] ^ y[0]) + __builtin_popcountll(x[1] ^ y[1]);
}
static block randVec(oc::PRNG& prng, u64 d)
{
    u64 w[2] = {prng.get<u64>(), prng.get<u64>()};
    if (d < 128) {
        if (d <= 64) { w[1] = 0; if (d < 64) w[0] &= (1ull << d) - 1; }
        else w[1] &= (1ull << (d - 64)) - 1;
    }
    block b;
    std::memcpy(&b, w, 16);
    return b;
}

// Enumerate the Hamming ball of radius t around c (all supports of size <= t), appending to out.
static void expandBall(block c, u64 d, u64 t, std::vector<block>& out)
{
    out.push_back(c);
    std::vector<u64> idx;
    for (u64 w = 1; w <= t; ++w) {
        idx.resize(w);
        for (u64 i = 0; i < w; ++i) idx[i] = i;
        while (true) {
            block b = c;
            for (u64 i = 0; i < w; ++i) b = flipBit(b, idx[i]);
            out.push_back(b);
            // next combination
            i64 i = (i64)w - 1;
            while (i >= 0 && idx[i] == d - w + (u64)i) --i;
            if (i < 0) break;
            ++idx[i];
            for (u64 j = i + 1; j < w; ++j) idx[j] = idx[j - 1] + 1;
        }
    }
}

int main(int argc, char** argv)
{
    oc::CLP cmd(argc, argv);
    u64 role = cmd.getOr("role", 0ull);
    std::string addr = cmd.getOr<std::string>("addr", "127.0.0.1:1400");
    std::string mode = cmd.getOr<std::string>("mode", "synth");
    u64 m = cmd.getOr("m", 256ull), n = cmd.getOr("n", 256ull);
    u64 d = cmd.getOr("d", 64ull), t = cmd.getOr("delta", 3ull);
    u64 seed = cmd.getOr("seed", 1ull);
    u64 nt = cmd.getOr("nt", 1ull);
    u64 V = ballVol(d, t);
    u64 NR = cmd.getOr("nr", n * V);
    if (d > 128) { std::cerr << "d must be <= 128\n"; return 1; }

    std::vector<block> inputs;
    std::vector<block> X, Y; // real mode
    double prepWall = 0, prepCpu = 0;
    u64 uniqBall = 0;

    double pc0 = cpuNow(), pw0 = nowS();
    if (mode == "synth") {
        // Receiver item i = AES_k(0, i); sender item j = AES_k(0, j*stride) for j < plant, else AES_k(1, j).
        oc::AES aes(block(seed, 0x5eed));
        u64 plant = cmd.getOr("plant", std::min<u64>(m / 8, NR));
        u64 stride = std::max<u64>(1, NR / std::max<u64>(1, plant));
        if (role == 1) {
            inputs.resize(NR);
            const u64 B = 1 << 12;
            std::vector<block> pt(B);
            for (u64 i = 0; i < NR; i += B) {
                u64 c = std::min(B, NR - i);
                for (u64 k = 0; k < c; ++k) pt[k] = block(0, i + k);
                aes.ecbEncBlocks(pt.data(), c, inputs.data() + i);
            }
        } else {
            inputs.resize(m);
            for (u64 j = 0; j < m; ++j)
                inputs[j] = aes.ecbEncBlock(j < plant ? block(0, j * stride) : block(1, j));
        }
        if (role == 1) std::cout << "planted=" << plant << "\n";
    } else {
        // Real data: both sides derive X and Y from the shared seed; each uses only its own part.
        oc::PRNG prng(block(seed, 99));
        X.resize(m); Y.resize(n);
        for (auto& x : X) x = randVec(prng, d);
        for (auto& y : Y) y = randVec(prng, d);
        // plant near pairs at distances 0..delta+2 so that both hits and near-misses occur
        u64 plant = std::min(m, n) / 4;
        for (u64 k = 0; k < plant; ++k) {
            u64 i = prng.get<u64>() % m, j = prng.get<u64>() % n;
            block y = X[i];
            u64 w = k % (t + 3);
            std::set<u64> pos;
            while (pos.size() < w) pos.insert(prng.get<u64>() % d);
            for (auto p : pos) y = flipBit(y, p);
            Y[j] = y;
        }
        if (role == 0) inputs = X;
        else {
            inputs.reserve(n * V);
            for (auto& y : Y) expandBall(y, d, t, inputs);
            std::sort(inputs.begin(), inputs.end(), [](const block& a, const block& b) {
                return std::memcmp(&a, &b, 16) < 0; });
            inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());
            uniqBall = inputs.size();
            // Pad with random dummies to exactly n*V. For d<128 dummies have a nonzero bit above
            // position d, so they can never equal a real d-bit item; for d=128 collisions are 2^-128-ish.
            oc::PRNG dp(oc::sysRandomSeed());
            while (inputs.size() < NR) {
                block b = dp.get<block>();
                if (d < 128) {
                    u64 w[2]; std::memcpy(w, &b, 16);
                    if (d < 64) w[0] |= (1ull << d); else w[1] |= (1ull << (d - 64));
                    std::memcpy(&b, w, 16);
                }
                inputs.push_back(b);
            }
            // shuffle so position carries no information
            for (u64 i = inputs.size() - 1; i > 0; --i) std::swap(inputs[i], inputs[dp.get<u64>() % (i + 1)]);
            // Dummies: real mode duplicates among dummies are negligible; RsPsi requires distinct items.
        }
    }
    prepWall = nowS() - pw0; prepCpu = cpuNow() - pc0;

    auto sock = coproto::asioConnect(addr, role == 1);
    double c0 = cpuNow(), w0 = nowS();
    std::vector<u64> inter;
    if (role == 0) {
        RsPsiSender s;
        s.init(m, NR, 40, oc::sysRandomSeed(), false, nt);
        macoro::sync_wait(s.run(inputs, sock));
    } else {
        RsPsiReceiver r;
        r.init(m, NR, 40, oc::sysRandomSeed(), false, nt);
        macoro::sync_wait(r.run(inputs, sock));
        inter = r.mIntersection;
    }
    macoro::sync_wait(sock.flush());
    double wall = nowS() - w0, cpu = cpuNow() - c0;

    std::cout << "role=" << (role ? "R(receiver,learns)" : "S(sender)") << " mode=" << mode << " m=" << m
              << " n=" << n << " d=" << d << " delta=" << t << " V=" << V << " NR=" << NR << " nt=" << nt << "\n"
              << "prep_wall_s=" << prepWall << " prep_cpu_s=" << prepCpu << "\n"
              << "psi_wall_s=" << wall << " psi_cpu_s=" << cpu << " sent_MiB=" << sock.bytesSent() / double(1 << 20)
              << " recv_MiB=" << sock.bytesReceived() / double(1 << 20) << " sent_B=" << sock.bytesSent()
              << " recv_B=" << sock.bytesReceived() << " peak_rss_MiB=" << peakRssMiB() << "\n";

    if (role == 1) {
        std::cout << "intersection_size=" << inter.size() << "\n";
        if (mode == "real") {
            std::set<std::pair<u64, u64>> got, truth;
            auto key = [](block b) { u64 w[2]; std::memcpy(w, &b, 16); return std::make_pair(w[1], w[0]); };
            for (auto i : inter) got.insert(key(inputs[i]));
            for (auto& x : X) {
                bool hit = false;
                for (auto& y : Y) if (hd(x, y) <= t) { hit = true; break; }
                if (hit) truth.insert(key(x));
            }
            u64 fn = 0, fp = 0;
            for (auto& v : truth) fn += !got.count(v);
            for (auto& v : got) fp += !truth.count(v);
            std::cout << "real_check: union_before_padding=" << uniqBall << " padded_to=" << inputs.size()
                      << " got=" << got.size() << " truth_bruteforce=" << truth.size() << " false_neg=" << fn
                      << " false_pos=" << fp << (fn || fp ? " FAIL" : " PASS") << "\n";
            return (fn || fp) ? 2 : 0;
        }
    }
    return 0;
}
