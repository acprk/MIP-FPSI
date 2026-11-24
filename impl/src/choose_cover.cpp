// Receiver-side cover selection with the protocol's own public permutation (Sec. "Choosing the cover").
//   choose_cover -data file.bin -d 64 -delta 3 [-pow2]
// Uses only Y (the receiver's set), the public parameters and the public sender size m. Among
// minimal block covers (b,s) with r <= 1 and T <= 200 it minimises the threshold-circuit work
// N * (cmax + r + tau), N = cuckoo bins for m*Q probes, Q = B * sum_t V(|S_t|, r).
// With -pow2 the rule is applied to the rounded loads 2^ceil(log2 B) (leakage-optimal mode).
#include "fpsi.h"

#include <cryptoTools/Common/CLP.h>
#include <cryptoTools/Common/CuckooIndex.h>

#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>

using namespace mip;

namespace
{
    double log2Vol(u64 len, u64 radius)
    {
        double v = 0, c = 1;
        for (u64 e = 0; e <= std::min(radius, len); ++e)
        {
            v += c;
            c = c * double(len - e) / double(e + 1);
        }
        return std::log2(v);
    }

    // (b,s) block cover is minimal iff some error distribution over the b blocks (sum <= delta)
    // leaves exactly one s-subset of blocks with <= r errors (all tables are symmetric).
    bool minimal(u64 b, u64 s, u64 delta, u64 r)
    {
        std::vector<u64> e(b, 0);
        std::vector<std::vector<u64>> subsets;
        std::vector<u64> cur;
        std::function<void(u64)> gen = [&](u64 i) {
            if (cur.size() == s) { subsets.push_back(cur); return; }
            for (u64 j = i; j < b; ++j) { cur.push_back(j); gen(j + 1); cur.pop_back(); }
        };
        gen(0);
        bool found = false;
        std::function<void(u64, u64)> rec = [&](u64 i, u64 left) {
            if (found) return;
            if (i == b)
            {
                u64 cnt = 0;
                for (auto& A : subsets)
                {
                    u64 sum = 0;
                    for (auto k : A) sum += e[k];
                    if (sum <= r && ++cnt > 1) break;
                }
                if (cnt == 1) found = true;
                return;
            }
            for (u64 v = 0; v <= left; ++v) { e[i] = v; rec(i + 1, left - v); }
            e[i] = 0;
        };
        rec(0, delta);
        return found;
    }
}

int main(int argc, char** argv)
{
    oc::CLP cmd(argc, argv);
    u64 d = cmd.getOr("d", 64ull), delta = cmd.getOr("delta", 3ull), ssp = 40;
    bool pow2 = cmd.isSet("pow2");
    std::ifstream in(cmd.get<std::string>("data"), std::ios::binary);
    u64 m, n, dd;
    in.read((char*)&m, 8); in.read((char*)&n, 8); in.read((char*)&dd, 8);
    if (dd != d) { std::cerr << "d mismatch\n"; return 1; }
    u64 db = d / 8;
    ItemMatrix X(m, db), Y0(n, db);
    in.read((char*)X.data(), m * db);
    in.read((char*)Y0.data(), n * db);
    auto Y = uniqueRows(Y0);
    m = uniqueRows(X).rows(); // public size after the sender's local dedup

    FpsiConfig def;
    double best = -1;
    u64 bb = 0, bs = 0;
    for (u64 b = 2; b < std::min<u64>(d, 3 * delta + 4); ++b)
        for (u64 s = 1; s < b; ++s)
        {
            u64 r = MipParams::radius(b, s, delta);
            if (r > 1) continue;
            double T = std::round(std::tgamma(double(b) + 1) / (std::tgamma(double(s) + 1) * std::tgamma(double(b - s) + 1)));
            if (T > 200 || !minimal(b, s, delta, r)) continue;
            auto p = MipParams::make(d, delta, b, s, def.publicSeed);
            u64 B = receiverMap(p, Y).B;
            if (pow2) { u64 q = 1; while (q < B) q <<= 1; B = q; }
            u64 Q = p.probesPerElement(B);
            u64 N = oc::CuckooIndex<>::selectParams(m * Q, ssp, 0, 3).numBins();
            u64 dc = p.cmax + p.r;
            double t = double(ssp) + std::log2(double(N)) + log2Vol(dc, delta) - double(dc);
            u64 tau = t > 0 ? u64(std::ceil(t)) : 0;
            double work = double(N) * double(dc + tau);
            std::cout << "(b,s)=(" << b << "," << s << ") r=" << r << " T=" << T << " B=" << B << " Q=" << Q
                      << " tau=" << tau << " work=" << work << "\n";
            if (best < 0 || work < best) { best = work; bb = b; bs = s; }
        }
    std::cout << "choice: (b,s)=(" << bb << "," << bs << ") work=" << best << (pow2 ? " [pow2]" : "") << "\n";
    return 0;
}
