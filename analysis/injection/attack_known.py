"""Knowledge (a): the attacker knows Y minus the protected vector exactly.

For each trial: pick a random (i, t) with y_i alone in its table-t bucket z (so that the
predicate 'Y contains a vector with key z in table t' differs between the hypotheses),
Y1 = Y, Y0 = Y with y_i replaced by a pool vector whose table-t key is not z.
The attacker computes the competing max load C of (Y minus y_i) union A, picks k per mode
(choose_k), injects k distinct vectors with key z in table t, and decides from the leakage.

usage: attack_known.py DATASET [TRIALS] [DP_DRAWS] [SEED]
writes results/known_<DATASET>.csv (one row per trial x mode)
"""
import csv
import os
import sys
import time
import zlib

import numpy as np

import datasets
from common import (Injector, Loads, choose_k, decide, dp_shift, leak, pick_replacement,
                    scenario_loads, keys_of)

MODE_EPS = [("exact", None), ("rounded", None), ("bound", None),
            ("dp", 1.0), ("dp", 0.1), ("dprounded", 1.0), ("dprounded", 0.1)]


def run(name, trials, draws, seed):
    Y, pool, T, meta = datasets.get(name, seed)
    L = Loads(Y, T)
    Bbar = 2 * L.B
    pairs = L.load1_pairs()
    nT = len(T)
    frac1 = len(pairs) / (L.n * nT)
    rng = np.random.default_rng([seed, zlib.crc32(name.encode())])
    Yset = {r.tobytes() for r in Y}
    pool_keys = [keys_of(pool, pos) for pos in T]
    out = os.path.join(os.path.dirname(__file__), "results", f"known_{name}.csv")
    rows = []
    t0 = time.time()
    for tr in range(trials):
        i, t = pairs[int(rng.integers(len(pairs)))]
        z = int(L.keys[t][i])
        yrep = pick_replacement(pool, pool_keys[t], z, rng, Yset)
        inj = Injector(L, t, i, rng)
        for mode, eps in MODE_EPS:
            # adaptive: C depends on A (injected vectors land in other tables' buckets)
            _, _, C = scenario_loads(L, i, t, yrep, inj.grow(0))
            k = choose_k(mode, C, eps, Bbar)
            while True:
                B0, B1, C = scenario_loads(L, i, t, yrep, inj.grow(k))
                k2 = choose_k(mode, C, eps, Bbar)
                if mode == "bound" or k2 <= k:
                    break
                k = k2
            if mode in ("dp", "dprounded"):
                l0 = leak(mode, B0, rng, eps, size=draws)
                l1 = leak(mode, B1, rng, eps, size=draws)
                c0 = float(np.mean(~decide(mode, l0, k, eps)))
                c1 = float(np.mean(decide(mode, l1, k, eps)))
                n_obs = draws
            else:
                c0 = float(not decide(mode, leak(mode, B0, Bbar=Bbar), k))
                c1 = float(bool(decide(mode, leak(mode, B1, Bbar=Bbar), k)))
                n_obs = 1
            rows.append(dict(dataset=name, trial=tr, i=i, t=t, mode=mode,
                             eps="" if eps is None else eps, k=k, C=C, B0=B0, B1=B1,
                             Bbar=Bbar, correct0=c0, correct1=c1, n_obs=n_obs,
                             s_dp="" if eps is None else dp_shift(eps)))
        if (tr + 1) % 200 == 0:
            print(f"{name}: {tr + 1}/{trials} trials, {time.time() - t0:.0f}s", flush=True)
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    meta.update(B=L.B, Bbar=Bbar, load1_frac=round(frac1, 4), trials=trials, dp_draws=draws, seed=seed)
    with open(out.replace(".csv", ".meta.txt"), "w") as f:
        f.write(repr(meta) + "\n")
    print(meta, flush=True)


if __name__ == "__main__":
    name = sys.argv[1]
    trials = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
    draws = int(sys.argv[3]) if len(sys.argv) > 3 else 100
    seed = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    run(name, trials, draws, seed)
