// E6: plaintext candidate statistics of the public block cover (same MipParams::make and public
// seed as fpsi_bench/net_bench; no protocol is run).
//   cand_stats -name tag -d 128 -delta 4 -b 5 -s 1 [-m 4096 -n 4096 -seed 1 | -data file.bin]
// Synthetic data: the exact generator of fpsi_bench/net_bench (seed, plant = min(m,n)/8).
// X and Y are deduplicated (uniqueRows) as in the protocol. Appends one row to -csv and writes the
// pooled bucket-load histogram to -hist.
#include "fpsi.h"
#include "mip.h"

#include <cryptoTools/Common/CLP.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <unordered_map>

using namespace mip;

static u64 keyOf(const std::uint8_t* row, const std::vector<u32>& pos)
{
    u64 k = 0;
    for (u64 i = 0; i < pos.size(); ++i)
        k |= u64(getBit(row, pos[i])) << i;
    return k;
}

template <typename F>
static void ball(u64 key, u64 len, u64 r, u64 start, F&& f)
{
    f(key);
    if (r == 0) return;
    for (u64 k = start; k < len; ++k)
        ball(key ^ (u64(1) << k), len, r - 1, k + 1, f);
}

static double binom(u64 n, u64 k)
{
    double r = 1;
    for (u64 i = 1; i <= k; ++i) r = r * double(n - k + i) / double(i);
    return r;
}

int main(int argc, char** argv)
{
    oc::CLP cmd(argc, argv);
    FpsiConfig cfg;
    u64 d = cmd.getOr("d", 128ull), delta = cmd.getOr("delta", 4ull);
    u64 b = cmd.getOr("b", delta + 1), s = cmd.getOr("s", 1ull);
    std::string name = cmd.getOr<std::string>("name", "run");
    u64 m = cmd.getOr("m", 4096ull), n = cmd.getOr("n", 4096ull), db = d / 8;
    std::string src;
    ItemMatrix X0, Y0;
    if (cmd.isSet("data"))
    {
        src = cmd.get<std::string>("data");
        std::ifstream in(src, std::ios::binary);
        u64 dd;
        in.read((char*)&m, 8); in.read((char*)&n, 8); in.read((char*)&dd, 8);
        if (!in || dd != d) { std::cerr << "header does not match -d\n"; return 1; }
        X0.resize(m, db); Y0.resize(n, db);
        in.read((char*)X0.data(), m * db);
        in.read((char*)Y0.data(), n * db);
    }
    else
    {
        u64 seed = cmd.getOr("seed", 1ull);
        src = "fpsi_bench generator seed=" + std::to_string(seed);
        oc::PRNG prng(oc::block(seed, 99));
        X0.resize(m, db); Y0.resize(n, db);
        prng.get(X0.data(), X0.size());
        prng.get(Y0.data(), Y0.size());
        u64 plant = std::min(m, n) / 8;
        for (u64 t = 0; t < plant; ++t)
        {
            u64 i = prng.get<u64>() % m, j = prng.get<u64>() % n;
            std::memcpy(&Y0(j, 0), &X0(i, 0), db);
            u64 w = prng.get<u64>() % (delta + 3);
            for (u64 e = 0; e < w; ++e)
            {
                u64 bit = prng.get<u64>() % d;
                Y0(j, bit / 8) ^= std::uint8_t(1u << (bit % 8));
            }
        }
    }
    auto X = uniqueRows(X0), Y = uniqueRows(Y0);
    m = X.rows(); n = Y.rows();

    auto p = MipParams::make(d, delta, b, s, cfg.publicSeed);
    u64 T = p.numTables(), r = p.r;
    for (auto& t : p.tables)
        if (t.size() > 63) { std::cerr << "key longer than 63 bits\n"; return 1; }

    // receiver buckets per table
    std::vector<std::unordered_map<u64, std::vector<u32>>> bk(T);
    for (u64 t = 0; t < T; ++t)
        for (u64 j = 0; j < n; ++j)
            bk[t][keyOf(&Y(j, 0), p.tables[t])].push_back(u32(j));
    u64 B = 0;
    std::vector<u64> loads;
    std::map<u64, u64> hist;
    for (u64 t = 0; t < T; ++t)
        for (auto& kv : bk[t])
        {
            loads.push_back(kv.second.size());
            hist[kv.second.size()]++;
            B = std::max<u64>(B, kv.second.size());
        }
    std::sort(loads.begin(), loads.end());
    auto pct = [&](double q) { return loads[std::min<u64>(loads.size() - 1, u64(std::ceil(q * loads.size())) - 1)]; };
    double meanLoad = double(std::accumulate(loads.begin(), loads.end(), u64(0))) / loads.size();

    double sumV = 0, est = 0;
    std::string ells;
    std::map<u64, u64> ellCount;
    for (u64 t = 0; t < T; ++t)
    {
        u64 l = p.tables[t].size();
        double V = 0;
        for (u64 e = 0; e <= r; ++e) V += binom(l, e);
        sumV += V;
        est += V / std::ldexp(1.0, int(l));
        ellCount[l]++;
    }
    for (auto& kv : ellCount) ells += std::to_string(kv.first) + "x" + std::to_string(kv.second) + " ";
    if (u64(sumV) != p.ballSum()) { std::cerr << "ballSum mismatch\n"; return 1; }
    est *= double(m) * double(n);

    // candidates per sender vector
    std::vector<u32> stamp(n, 0);
    std::vector<u32> cand;
    u64 candPairs = 0, trueCand = 0, nePerXSum = 0, nePerXMax = 0, probeHits = 0, outSize = 0, falseNear = 0;
    for (u64 i = 0; i < m; ++i)
    {
        cand.clear();
        u64 ne = 0;
        for (u64 t = 0; t < T; ++t)
        {
            u64 k0 = keyOf(&X(i, 0), p.tables[t]);
            ball(k0, p.tables[t].size(), r, 0, [&](u64 z) {
                auto it = bk[t].find(z);
                if (it == bk[t].end()) return;
                ++ne;
                probeHits += it->second.size();
                for (auto j : it->second)
                    if (stamp[j] != i + 1) { stamp[j] = u32(i + 1); cand.push_back(j); }
            });
        }
        nePerXSum += ne;
        nePerXMax = std::max(nePerXMax, ne);
        candPairs += cand.size();
        bool hit = false;
        for (auto j : cand)
        {
            u64 h = hamming(&X(i, 0), &Y(j, 0), d);
            if (h <= delta) { ++trueCand; hit = true; }
            else if (h <= 2 * delta) ++falseNear;
        }
        outSize += hit;
    }
    // completeness check: brute-force count of all pairs within delta
    u64 truePairs = 0;
    for (u64 i = 0; i < m; ++i)
        for (u64 j = 0; j < n; ++j)
            truePairs += hamming(&X(i, 0), &Y(j, 0), d) <= delta;

    std::string csv = cmd.getOr<std::string>("csv", "cand_stats.csv");
    bool hdr = !std::ifstream(csv).good();
    std::ofstream o(csv, std::ios::app);
    if (hdr)
        o << "name,source,d,delta,b,s,m_dedup,n_dedup,T,ell_t(len x count),r,sumV,B,Q,mQ,"
             "nonempty_probed_mean,nonempty_probed_max,probe_hits_total,cand_pairs,true_cand_pairs,"
             "false_cand_pairs,false_cand_delta_lt_H_le_2delta,true_pairs_bruteforce,complete,output_size,uniform_estimate,"
             "nonempty_buckets,load_max,load_p99,load_p90,load_mean\n";
    o << name << "," << src << "," << d << "," << delta << "," << b << "," << s << "," << m << "," << n << ","
      << T << "," << ells << "," << r << "," << sumV << "," << B << "," << B * u64(sumV) << "," << m * B * u64(sumV) << ","
      << double(nePerXSum) / m << "," << nePerXMax << "," << probeHits << "," << candPairs << "," << trueCand << ","
      << candPairs - trueCand << "," << falseNear << "," << truePairs << "," << (truePairs == trueCand ? "yes" : "NO") << ","
      << outSize << "," << est << "," << loads.size() << "," << loads.back() << "," << pct(0.99) << ","
      << pct(0.90) << "," << meanLoad << "\n";
    std::ofstream h(cmd.getOr<std::string>("hist", name + "_loadhist.csv"));
    h << "load,num_buckets\n";
    for (auto& kv : hist) h << kv.first << "," << kv.second << "\n";
    std::cout << name << " T=" << T << " ell=" << ells << " r=" << r << " sumV=" << sumV << " B=" << B
              << " Q=" << B * u64(sumV) << " ne_mean=" << double(nePerXSum) / m << " ne_max=" << nePerXMax
              << " cand=" << candPairs << " true=" << trueCand << " false=" << candPairs - trueCand
              << " false_near(H<=2delta)=" << falseNear << " bruteforce_true=" << truePairs
              << " out=" << outSize << " m=" << m << " n=" << n << " est=" << est << " load max/p99/p90/mean="
              << loads.back() << "/" << pct(0.99) << "/" << pct(0.90) << "/" << meanLoad << "\n";
    return 0;
}
