"""Receiver-side parameter selection: uses ONLY the receiver's set Y (and public d, delta, m).
For each candidate (b,s) with r(b,s,delta) <= 1, compute B(Y) and the estimated threshold-circuit
work per sender vector  1.27 * Q * (cmax + r),  Q = B * sum_t V(|S_t|, r).  Pick the minimum.
The chosen (b,s) is a function of Y and is therefore part of the leakage to the sender."""
import sys
from itertools import combinations
from math import ceil, comb, log2

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from bu_stats import load, radius, tables  # noqa: E402
from collections import Counter  # noqa: E402

def compositions(total, parts):
    """all (e_1..e_parts) with e_i >= 0 and sum <= total"""
    if parts == 0:
        yield ()
        return
    for e in range(total + 1):
        for rest in compositions(total - e, parts - 1):
            yield (e,) + rest


def minimal(b, s, delta, r):
    """block cover (b,s) is minimal iff some error distribution over the blocks (sum <= delta)
    leaves exactly one s-subset of blocks with <= r errors (tables are symmetric)."""
    subsets = list(combinations(range(b), s))
    for e in compositions(delta, b):
        if sum(1 for A in subsets if sum(e[i] for i in A) <= r) == 1:
            return True
    return False


def tag_bits(dc, delta, N, ssp=40):
    vol = sum(comb(dc, e) for e in range(min(delta, dc) + 1))
    return max(0, ceil(ssp + log2(N) + log2(vol) - dc))


if __name__ == "__main__":
    _, Y, d = load(sys.argv[1])
    delta = int(sys.argv[2])
    m = int(sys.argv[3])  # public sender set size
    pow2 = len(sys.argv) > 4 and sys.argv[4] == "pow2"  # rule applied to rounded loads
    Y = np.unique(Y, axis=0)
    best = None
    for b in range(delta + 1, min(d, 3 * delta + 4)):
        for s in range(1, b):
            r = radius(b, s, delta)
            if r > 1:
                continue
            T = tables(d, b, s)
            if len(T) > 200 or not minimal(b, s, delta, r):
                continue
            B = max(max(Counter(map(bytes, Y[:, pos])).values()) for pos in T)
            if pow2:
                B = 1 << (B - 1).bit_length()
            Q = B * sum(comb(len(p), e) for p in T for e in range(r + 1))
            dc = d - min(len(p) for p in T) + r
            N = 1.3 * m * Q
            tau = tag_bits(dc, delta, N)
            work = N * (dc + tau)
            print(f"(b,s)=({b},{s}) r={r} T={len(T)} B={B} Q={Q} tau={tau} work={work:.3g}", flush=True)
            if best is None or work < best[0]:
                best = (work, b, s, B, Q)
    print(f"choice: (b,s)=({best[1]},{best[2]}) B={best[3]} Q={best[4]} work={best[0]:.3g}")
