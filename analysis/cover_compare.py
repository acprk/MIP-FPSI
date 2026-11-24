"""Compare coordinate covers on a dataset: block pigeonhole (b,s) vs Pagh's CoveringLSH.
CoveringLSH (Pagh, SODA'16): m: [d] -> {0,1}^{c}, c = delta+1, uniformly random; for each
nonzero v in {0,1}^c the table key positions are K_v = { i : <m(i), v> = 1 (mod 2) }.
If x,y differ on a set D with |D| <= delta, the vectors {m(i)}_{i in D} span <= delta < c dims,
so some nonzero v is orthogonal to all of them and x|K_v = y|K_v (radius 0).
Partitioned variant (Pagh Sec. 4): split [d] into q parts, cover each part for radius
floor(delta/q) with c_q = floor(delta/q)+1 dims (pigeonhole across parts).
Reports T, mean key length, B(Y) (dedup), Q = B * #probes, and an exact-coverage self-check."""
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
    up = lambda A: np.unique(np.unpackbits(A, axis=1, bitorder="little"), axis=0)
    return up(X), up(Y), d


def pagh_tables(d, delta, q, seed=7):
    rng = np.random.default_rng(seed)
    perm = rng.permutation(d)
    parts = np.array_split(perm, q)
    rad = delta // q
    c = rad + 1
    tables = []
    for part in parts:
        mvec = rng.integers(0, 2, size=(len(part), c))
        for v in range(1, 2 ** c):
            vb = np.array([(v >> k) & 1 for k in range(c)])
            pos = part[(mvec @ vb) % 2 == 1]
            tables.append(np.sort(pos))
    return tables


def block_tables(d, b, s, seed=1):
    perm = np.random.default_rng(seed).permutation(d)
    blocks = [perm[i * d // b:(i + 1) * d // b] for i in range(b)]
    return [np.sort(np.concatenate([blocks[i] for i in c])) for c in combinations(range(b), s)]


def radius(b, s, delta):
    q, rem = divmod(delta, b)
    return s * q + max(0, s + rem - b)


def stats(Y, tables, r):
    B = max(max(Counter(map(bytes, Y[:, pos])).values()) for pos in tables)
    probes = sum(sum(comb(len(p), e) for e in range(r + 1)) for p in tables)
    cmax = Y.shape[1] - min(len(p) for p in tables)
    return B, probes * B, cmax


def check_cover(d, delta, tables, r, trials=2000, seed=3):
    rng = np.random.default_rng(seed)
    for _ in range(trials):
        D = rng.choice(d, size=delta, replace=False)
        diff = np.zeros(d, dtype=bool)
        diff[D] = True
        if not any(diff[p].sum() <= r for p in tables):
            return False
    return True


if __name__ == "__main__":
    if sys.argv[1].startswith("uniform:"):
        _, dd, nn = sys.argv[1].split(":")
        d, nn = int(dd), int(nn)
        rng = np.random.default_rng(11)
        Y = rng.integers(0, 2, size=(nn, d), dtype=np.uint8)
        X = Y[:1]
    else:
        X, Y, d = load(sys.argv[1])
    delta = int(sys.argv[2])
    print(f"dedup m={len(X)} n={len(Y)} d={d} delta={delta}")
    rows = []
    for b, s in [(delta + 1, 1), (delta // 2 + 1, 1), (3, 1), (2, 1)]:
        if b > d or s >= b:
            continue
        r = radius(b, s, delta)
        if sum(comb(d // b + 1, e) for e in range(r + 1)) * b > 50000:
            continue
        T = block_tables(d, b, s)
        B, Q, cmax = stats(Y, T, r)
        rows.append((f"block({b},{s}) r={r}", len(T), d - cmax, B, Q, check_cover(d, delta, T, r)))
    for q in (1, 2, 3, 4, 8):
        if (delta // q + 1) > 12:
            continue
        T = pagh_tables(d, delta, q)
        r = 0
        B, Q, cmax = stats(Y, T, r)
        rows.append((f"pagh(q={q}) c={delta // q + 1}", len(T), d - cmax, B, Q, check_cover(d, delta, T, 0)))
    for name, T, keymin, B, Q, ok in rows:
        print(f"{name:22s} T={T:5d} min_key={keymin:4d} B={B:6d} Q={Q:8d} 1.27Q/n={1.27 * Q / len(Y):.4f} cover_ok={ok}")
