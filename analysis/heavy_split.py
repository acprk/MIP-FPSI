"""Heavy-bucket offloading trade-off.
For threshold B0, every receiver item lying in some bucket of load > B0 (in any table) is moved to
a heavy set H handled by all-pairs comparison. Per sender element the circuit count is
    1.27 * T * V * B0  (MIP part, cuckoo factor 1.27)  +  |H|  (all-pairs part).
Loads are recomputed after removal (iterated until stable)."""
import sys
from collections import Counter, defaultdict
from math import comb

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from bu_stats import load, radius, tables  # noqa: E402


def split(Y, T, B0):
    alive = np.ones(len(Y), dtype=bool)
    keys = [list(map(bytes, Y[:, pos])) for pos in T]
    while True:
        changed = False
        for kt in keys:
            groups = defaultdict(list)
            for j in np.nonzero(alive)[0]:
                groups[kt[j]].append(j)
            for g in groups.values():
                if len(g) > B0:
                    alive[g] = False
                    changed = True
        if not changed:
            return int((~alive).sum())


if __name__ == "__main__":
    X, Y, d = load(sys.argv[1])
    delta = int(sys.argv[2])
    Y = np.unique(Y, axis=0)
    X = np.unique(X, axis=0)
    print(f"dedup m={len(X)} n={len(Y)}")
    for cfg in sys.argv[3:]:
        b, s = map(int, cfg.split(","))
        r = radius(b, s, delta)
        T = tables(d, b, s)
        V = sum(comb(len(p), e) for p in T for e in range(r + 1)) / len(T)
        best = None
        for B0 in [1, 2, 3, 4, 6, 8, 12, 16, 24, 32]:
            h = split(Y, T, B0)
            cost = 1.27 * len(T) * V * B0 + h
            print(f"  b={b} s={s} B0={B0:3d} |H|={h:5d} circuits/elt={cost:8.1f}", flush=True)
            if best is None or cost < best[0]:
                best = (cost, B0, h)
        print(f"  best b={b} s={s}: B0={best[1]} |H|={best[2]} circuits/elt={best[0]:.1f}")
