"""Simulate the Hamming fuzzy mapping of Gao et al. (AC'24) on real data and count missed matches.
Receiver: split y into ceil(d/P) super-components of P bits; register y under (position, value) for
up to delta+1 components whose value is unique in Y at that position (if fewer exist, all unique
ones -- the most favourable reading when UniqC fails). Sender: queries all its components.
x is found iff it shares a registered (position, value) with some y within distance delta.
We report, for each P, the fraction of x in I that the mapping misses."""
import struct
import sys
from collections import Counter, defaultdict

import numpy as np


def load(path):
    with open(path, "rb") as f:
        m, n, d = struct.unpack("<QQQ", f.read(24))
        X = np.frombuffer(f.read(m * d // 8), dtype=np.uint8).reshape(m, d // 8)
        Y = np.frombuffer(f.read(n * d // 8), dtype=np.uint8).reshape(n, d // 8)
    up = lambda A: np.unique(np.unpackbits(A, axis=1, bitorder="little"), axis=0)
    return up(X), up(Y), d


def truth(X, Y, delta, d):
    # exact via pigeonhole index on delta+1 contiguous blocks
    nb = delta + 1
    idx = [defaultdict(list) for _ in range(nb)]
    cuts = [k * d // nb for k in range(nb + 1)]
    for j, y in enumerate(Y):
        for t in range(nb):
            idx[t][y[cuts[t]:cuts[t + 1]].tobytes()].append(j)
    pairs = defaultdict(set)
    for i, x in enumerate(X):
        for t in range(nb):
            for j in idx[t].get(x[cuts[t]:cuts[t + 1]].tobytes(), ()):
                if int((x != Y[j]).sum()) <= delta:
                    pairs[i].add(j)
    return pairs


if __name__ == "__main__":
    X, Y, d = load(sys.argv[1])
    delta = int(sys.argv[2])
    pairs = truth(X, Y, delta, d)
    print(f"m={len(X)} n={len(Y)} |I|={len(pairs)}")
    best = None
    for P in range(4, d // (delta + 1) + 1):
        starts = list(range(0, d, P))
        comps = [Y[:, k:k + P] for k in starts]
        keysY = [[c[j].tobytes() for j in range(len(Y))] for c in comps]
        cnts = [Counter(k) for k in keysY]
        reg = set()
        for j in range(len(Y)):
            chosen = [c for c in range(len(comps)) if cnts[c][keysY[c][j]] == 1][: delta + 1]
            for c in chosen:
                reg.add((c, keysY[c][j], j))
        regmap = defaultdict(set)
        for c, v, j in reg:
            regmap[(c, v)].add(j)
        missed = 0
        for i, js in pairs.items():
            x = X[i]
            cand = set()
            for c, k in enumerate(starts):
                cand |= regmap.get((c, x[k:k + P].tobytes()), set())
            if not (cand & js):
                missed += 1
        rate = missed / max(1, len(pairs))
        print(f"P={P:3d} comps={len(starts):3d} missed {missed}/{len(pairs)} = {rate:.4f}", flush=True)
        if best is None or rate < best[1]:
            best = (P, rate, missed)
    print(f"best P={best[0]}: Gao's mapping misses {best[2]}/{len(pairs)} = {best[1]:.4f} of the true output")
