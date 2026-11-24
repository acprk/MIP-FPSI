#!/usr/bin/env python3
"""Certificate for Theorem 3(c): load intervals [b0,b1] on which Lemma S1 realises (2delta+1)-separated inputs.

Lemma S1 needs, for a critical table t with f free coordinates:
  b <= min(n, 2^f),  2nT <= b * 2^{min ell},  n V(d,2delta) + (n+m)(V(d,delta)+1) <= 2^{d-2},
and for separation a code of length f, size b, distance 2delta+1; we use the Gilbert-Varshamov certificate
b V(f,2delta) <= 2^f.
Rounded labels: ceil(log2 B) for B in [1,n] -> L+1 values, L = ceil(log2 n).
Competitor (Thm 3(b)): at least floor(log_{F+1}(b1/b0)) + 1 values.
Simplified bound log2 log2(F+1) + 1 holds whenever log2(b1/b0) >= (L+1)/2.
"""
from math import comb, log2, ceil, floor, log


def V(n, r):
    return sum(comb(n, i) for i in range(0, min(n, r) + 1))


def cert(name, d, delta, n, m, T, min_ell, f, Fs):
    cond2 = n * V(d, 2 * delta) + (n + m) * (V(d, delta) + 1) <= 2 ** (d - 2)
    b0 = max(1, ceil(2 * n * T / 2 ** min_ell))
    b1 = 0
    for b in range(1, min(n, 2 ** f) + 1):
        if b * V(f, 2 * delta) <= 2 ** f:
            b1 = b
    L = ceil(log2(n))
    ours = L + 1
    print(f"{name}: d={d} delta={delta} n={n} T={T} min_ell={min_ell} f={f}")
    print(f"  global condition n V(d,2d)+(n+m)(V(d,d)+1) <= 2^(d-2): {cond2}")
    print(f"  certified loads [b0,b1] = [{b0},{b1}],  log2(b1/b0) = {log2(b1/b0):.2f},  (L+1)/2 = {(L+1)/2}")
    print(f"  simplified bound applies: {log2(b1/b0) >= (L+1)/2}")
    for F in Fs:
        comp = floor(log(b1 / b0) / log(F + 1) + 1e-12) + 1
        excess = log2(ours / comp)
        simple = log2(log2(F + 1)) + 1
        print(f"  F={F}: rounded labels {ours} ({log2(ours):.2f} bits), competitor >= {comp} ({log2(comp):.2f} bits),"
              f" excess <= {excess:.2f} bits, simplified bound {simple:.2f}")


if __name__ == "__main__":
    n = m = 2 ** 12
    # (128,4), cover (5,1): 5 blocks of 25/26 bits, r=0, witness has D inside S-bar_t, j_t=0, L_t=128-26=102 worst
    cert("(128,4) cover (5,1)", 128, 4, n, m, 5, 25, 128 - 26 - 4, [10])
    # (128,16), cover (9,1): 9 blocks of 14/15 bits, r=1, j_t=0 (forced), table with ell=14 -> L_t=114
    cert("(128,16) cover (9,1)", 128, 16, n, m, 9, 14, 128 - 14 - 16, [274])
    # same cover, but a critical table with a 15-bit key (f = 97): weaker certificate, for comparison
    cert("(128,16) cover (9,1), 15-bit table", 128, 16, n, m, 9, 14, 128 - 15 - 16, [274])
