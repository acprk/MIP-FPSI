"""B (max receiver bucket load), U (max sender probe multiplicity) and probe count Q = sum_t V_t * B
for a dataset in fpsi_bench format, over several (b,s) and with/without exact-duplicate removal.
Mirrors impl/src/mip.cpp (same block construction up to the permutation seed)."""
import struct
import sys
from collections import Counter
from itertools import combinations
from math import comb

import numpy as np


def load(path):
    with open(path, "rb") as f:
        m, n, d = struct.unpack("<QQQ", f.read(24))
        X = np.frombuffer(f.read(m * d // 8), dtype=np.uint8).reshape(m, d // 8)
        Y = np.frombuffer(f.read(n * d // 8), dtype=np.uint8).reshape(n, d // 8)
    unpack = lambda A: np.unpackbits(A, axis=1, bitorder="little")
    return unpack(X), unpack(Y), d


def radius(b, s, delta):
    q, rem = divmod(delta, b)
    return s * q + max(0, s + rem - b)


def tables(d, b, s, seed=1):
    perm = np.random.default_rng(seed).permutation(d)
    blocks = [perm[i * d // b:(i + 1) * d // b] for i in range(b)]
    return [np.concatenate([blocks[i] for i in c]) for c in combinations(range(b), s)]


def stats(X, Y, d, delta, b, s):
    r = radius(b, s, delta)
    T = tables(d, b, s)
    B = 0
    U = 0
    Vsum = 0
    for pos in T:
        ky = Counter(map(bytes, Y[:, pos]))
        B = max(B, max(ky.values()))
        Vsum += sum(comb(len(pos), e) for e in range(r + 1))
        if r == 0:
            kx = Counter(map(bytes, X[:, pos]))
            U = max(U, max(kx.values()))
    return dict(b=b, s=s, r=r, T=len(T), B=B, U_r0=U if r == 0 else None, Q=Vsum * B)


if __name__ == "__main__":
    X, Y, d = load(sys.argv[1])
    delta = int(sys.argv[2])
    configs = [tuple(map(int, c.split(","))) for c in sys.argv[3:]]
    for dedup in (False, True):
        Xs, Ys = (np.unique(X, axis=0), np.unique(Y, axis=0)) if dedup else (X, Y)
        print(f"dedup={dedup}: m={len(Xs)} n={len(Ys)}")
        for b, s in configs:
            st = stats(Xs, Ys, d, delta, b, s)
            st["Q_x_m"] = st["Q"] * len(Xs)
            print("  ", st, flush=True)
