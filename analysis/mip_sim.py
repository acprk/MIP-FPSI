"""Plaintext simulator of the multi-index probing map (MIP).

Checks C1 (zero false negatives vs brute force) and reports the cost drivers of C2/C5:
B (max receiver bucket load), U (max multiplicity of a sender probe key), Q = b*V(r)*B probes
per sender element, #GMW circuits ~ 1.27*m*Q, receiver OPPRF program size n*b*U.

usage: mip_sim.py <dataset> [d delta]
  dataset: uniform | clustered | npz:<path with arrays X,Y (uint8 bits)>
"""
import sys
from collections import Counter
from itertools import combinations
from math import comb

import numpy as np


def blocks_of(d, b, seed=1):
    perm = np.random.default_rng(seed).permutation(d)
    return [np.sort(p) for p in np.array_split(perm, b)]


def pack(bits):
    return int("".join(map(str, bits.tolist())), 2) if len(bits) else 0


def ball(v, nbits, r):
    out = [v]
    for e in range(1, r + 1):
        for pos in combinations(range(nbits), e):
            w = v
            for p in pos:
                w ^= 1 << p
            out.append(w)
    return out


def simulate(X, Y, d, delta, b):
    r = delta // b
    blks = blocks_of(d, b)
    # receiver buckets
    bucket = Counter()
    rkeys = {}
    for j, y in enumerate(Y):
        for beta, idx in enumerate(blks):
            k = (beta, pack(y[idx]))
            rkeys.setdefault(k, []).append(j)
            bucket[k] += 1
    B = max(bucket.values())
    # sender probes
    probe_mult = Counter()
    cand = 0
    found = set()
    for i, x in enumerate(X):
        hit = set()
        for beta, idx in enumerate(blks):
            for z in ball(pack(x[idx]), len(idx), r):
                k = (beta, z)
                probe_mult[k] += 1
                for j in rkeys.get(k, ()):
                    hit.add(j)
        cand += len(hit)
        if any(int((x != Y[j]).sum()) <= delta for j in hit):
            found.add(i)
    U = max(probe_mult.values())
    V = sum(comb(len(idx), e) for idx in blks for e in range(r + 1)) / b
    Q = int(b * V * B)
    m, n = len(X), len(Y)
    D = (X[:, None, :] != Y[None, :, :]).sum(2) if m * n <= 2 ** 26 else None
    truth = set(np.nonzero((D <= delta).any(1))[0].tolist()) if D is not None else None
    fn = len(truth - found) if truth is not None else "n/a"
    return dict(b=b, r=r, B=B, U=U, Q=Q, gmw=int(1.27 * m * Q), prog=n * b * U,
                cand_true_pairs=cand, matches=len(found), false_neg=fn)


def load(name, d, m, n, rng):
    if name == "uniform":
        return rng.integers(0, 2, (m, d), dtype=np.uint8), rng.integers(0, 2, (n, d), dtype=np.uint8)
    if name == "clustered":
        C = rng.integers(0, 2, (10, d), dtype=np.uint8)

        def draw(k):
            c = C[rng.integers(0, 10, k)]
            return c ^ (rng.random((k, d)) < 0.03).astype(np.uint8)
        return draw(m), draw(n)
    z = np.load(name.split(":", 1)[1])
    return z["X"].astype(np.uint8), z["Y"].astype(np.uint8)


if __name__ == "__main__":
    name = sys.argv[1]
    d = int(sys.argv[2]) if len(sys.argv) > 2 else 128
    delta = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    rng = np.random.default_rng(3)
    for m in [256, 1024, 4096]:
        X, Y = load(name, d, m, m, rng)
        if name.startswith("npz:"):
            m = len(X)
        # plant some true matches
        if not name.startswith("npz:"):
            for t in range(m // 8):
                Y[t] = X[t] ^ (np.isin(np.arange(d), rng.choice(d, rng.integers(0, delta + 1), replace=False))).astype(np.uint8)
        for b in sorted({delta + 1, (delta + 2) // 2}, reverse=True):
            res = simulate(X, Y, X.shape[1], delta, b)
            print(f"{name} m=n={m} d={X.shape[1]} δ={delta}: {res}", flush=True)
        if name.startswith("npz:"):
            break
