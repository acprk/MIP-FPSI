"""Fraction of receiver vectors violating Gao et al.'s UniqC assumption.
Gao packs P consecutive bits into one super-component (ceil(d/P) components) and needs, for every
receiver vector, delta+1 components whose value is unique among all receiver vectors at that
position. We report the violation rate for every P and the most favourable P."""
import struct
import sys
from collections import Counter

import numpy as np


def load_y(path):
    with open(path, "rb") as f:
        m, n, d = struct.unpack("<QQQ", f.read(24))
        f.seek(24 + m * d // 8)
        Y = np.frombuffer(f.read(n * d // 8), dtype=np.uint8).reshape(n, d // 8)
    return np.unique(np.unpackbits(Y, axis=1, bitorder="little"), axis=0), d


if __name__ == "__main__":
    Y, d = load_y(sys.argv[1])
    delta = int(sys.argv[2])
    best = None
    for P in range(4, d // (delta + 1) + 1):
        comps = [Y[:, k:k + P] for k in range(0, d, P)]
        if len(comps) < delta + 1:
            continue
        uniq = np.zeros((len(Y), len(comps)), dtype=bool)
        for c, A in enumerate(comps):
            keys = [bytes(r) for r in A]
            cnt = Counter(keys)
            uniq[:, c] = [cnt[k] == 1 for k in keys]
        viol = float((uniq.sum(1) < delta + 1).mean())
        print(f"P={P:3d} comps={len(comps):3d} violating={viol:.4f}")
        if best is None or viol < best[1]:
            best = (P, viol)
    print(f"n={len(Y)} d={d} delta={delta}: best P={best[0]} violation rate={best[1]:.4f}")
