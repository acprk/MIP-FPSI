"""Cross-check: the incremental loads used by the attack (scenario_loads) must equal B computed
from scratch on the explicit sets Y_b union A, and the predicate must differ between Y_0, Y_1.

usage: verify.py [TRIALS]
"""
import sys

import numpy as np

import datasets
from common import Injector, Loads, keys_of, pick_replacement, scenario_loads


def main(trials):
    rng = np.random.default_rng(99)
    for name in datasets.CONFIGS:
        Y, pool, T, meta = datasets.get(name)
        L = Loads(Y, T)
        pairs = L.load1_pairs()
        Yset = {r.tobytes() for r in Y}
        pool_keys = [keys_of(pool, pos) for pos in T]
        bad = 0
        for _ in range(trials):
            i, t = pairs[int(rng.integers(len(pairs)))]
            z = int(L.keys[t][i])
            yrep = pick_replacement(pool, pool_keys[t], z, rng, Yset)
            k = int(rng.integers(0, 80))
            A = Injector(L, t, i, rng).grow(k)
            B0, B1, _ = scenario_loads(L, i, t, yrep, A)
            Y1 = np.vstack([Y, A])
            Y0 = np.vstack([np.delete(Y, i, axis=0), yrep[None, :], A])
            assert len(np.unique(Y1, axis=0)) == len(Y1) and len(np.unique(Y0, axis=0)) == len(Y0)
            occ = lambda S: int((keys_of(S, T[t]) == z).sum())
            assert occ(np.vstack([np.delete(Y, i, axis=0), yrep[None, :]])) == 0
            assert occ(Y) == 1
            r0, r1 = Loads(Y0, T).B, Loads(Y1, T).B
            if (r0, r1) != (B0, B1):
                bad += 1
                print("MISMATCH", name, i, t, k, (B0, B1), (r0, r1))
        print(f"{name}: {trials} random trials, mismatches = {bad}")


if __name__ == "__main__":
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 40)
