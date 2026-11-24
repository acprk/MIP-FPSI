#!/usr/bin/env python3
"""Exact per-pair FN/FP rates of FLPSI-style subsampling (d=128, weight w=14, T=64 masks, need >= t=2
clean masks), assuming the T masks are independent uniformly random w-subsets of [d]."""
from math import comb

d, w, T, t = 128, 14, 64, 2


def p_clean(h):  # a mask avoids all h differing positions
    return comb(d - h, w) / comb(d, w)


def p_match(h):  # at least t of T masks clean
    p = p_clean(h)
    return 1 - sum(comb(T, k) * p**k * (1 - p) ** (T - k) for k in range(t))


for h in (0, 4, 8, 10, 16, 20, 25, 30):
    print(f"HD={h:3d}  p_clean={p_clean(h):.4g}  FNR(not matched)={1 - p_match(h):.3g}")
# FP: a uniformly random pair has HD ~ Bin(128, 1/2)
fp = sum(comb(d, h) / 2**d * p_match(h) for h in range(d + 1))
print(f"FPR for uniform random pair (match probability) = {fp:.3g}")
