"""Knowledge (b)/(c): the attacker does NOT know the background loads of Y.

Settings (k is chosen without looking at Y; the decision rule is the same as in knowledge (a)):
  shadow     attacker has a disjoint sample of the same distribution and the same size
             (synthetic: 20 fresh uniform sets; real data: Y is split into two random halves,
             receiver = one half, shadow = the other, 10 random splits).  C_hat = max shadow B.
  shadow2    as shadow, but with a 2x safety margin (attacker plans for load 2*C_hat).
  uniform    real data only: attacker wrongly assumes uniform data of the same size
             (20 uniform shadows); shows a misspecified prior.
  prior      no data knowledge at all: one earlier non-injected run of the same leakage mode
             on Y_b reveals an upper bound (exact B, 2^ceil(log2 B), or Btilde >= B); the attacker
             sets k from that leakage.  (Under DP this consumes a second eps of budget.)
  sweep      fixed k for all targets, no knowledge; advantage as a function of k.

usage: knowledge.py DATASET SETTING [TRIALS] [DP_DRAWS]
writes results/<setting>_<DATASET>.csv
"""
import csv
import os
import sys
import time
import zlib

import numpy as np

import datasets
from common import (Injector, Loads, clog2, decide, dp_shift, keys_of, leak, pick_replacement,
                    scenario_loads, two_sided_geometric, uniform)

MODE_EPS = [("exact", None), ("rounded", None), ("bound", None),
            ("dp", 1.0), ("dp", 0.1), ("dprounded", 1.0), ("dprounded", 0.1)]
HERE = os.path.dirname(os.path.abspath(__file__))


def k_from_upper(mode, U, eps, Bbar):
    """Injection size given an upper estimate U of the competing max load (need k >= U+1)."""
    if mode in ("exact", "dp"):
        return U + 1
    if mode == "rounded":
        return 1 << clog2(U + 1)
    if mode == "bound":
        return Bbar
    s = dp_shift(eps)
    return (1 << clog2(U + 1 + s)) - s


def outcome(mode, eps, B, b, k, Bbar, draws, rng):
    """Fraction of correct decisions for hypothesis b given the true load B."""
    if mode in ("dp", "dprounded"):
        dec = decide(mode, leak(mode, B, rng, eps, size=draws), k, eps)
        return float(np.mean(dec if b else ~dec))
    dec = bool(decide(mode, leak(mode, B, Bbar=Bbar), k))
    return float(dec if b else not dec)


def shadow_max(T, d, n, R, rng, source=None):
    if source is not None:
        return max(Loads(sh, T).B for sh in source)
    return max(Loads(uniform(n, d, rng), T).B for _ in range(R))


def instances(name, setting, rng):
    """Yields (Y_receiver, pool, T, meta, C_hat or None, tag)."""
    Y, pool, T, meta = datasets.get(name)
    synthetic = datasets.CONFIGS[name][0] == "uniform"
    d = Y.shape[1]
    if synthetic or setting in ("prior", "sweep"):
        if setting in ("shadow", "shadow2"):
            yield Y, pool, T, meta, shadow_max(T, d, len(Y), 20, rng), "full"
        elif setting == "uniform":
            return
        else:
            yield Y, pool, T, meta, None, "full"
        return
    # real data, shadow / uniform prior: split Y into halves
    for sp in range(10):
        perm = rng.permutation(len(Y))
        h = len(Y) // 2
        YA, YB = Y[perm[:h]], Y[perm[h:2 * h]]
        if setting in ("shadow", "shadow2"):
            C_hat = Loads(YB, T).B
        else:
            C_hat = shadow_max(T, d, h, 20, rng)
        m = dict(meta, n=h)
        yield YA, pool, T, m, C_hat, f"split{sp}"


def run(name, setting, trials, draws):
    rng = np.random.default_rng([7, zlib.crc32(f"{name}/{setting}".encode())])
    rows = []
    t0 = time.time()
    synthetic = datasets.CONFIGS[name][0] == "uniform"
    ksweep = list(range(1, 41)) + [48, 64, 96, 128]
    for Y, pool, T, meta, C_hat, tag in instances(name, setting, rng):
        L = Loads(Y, T)
        Bbar = 2 * L.B
        pairs = L.load1_pairs()
        Yset = {r.tobytes() for r in Y}
        pool_keys = [keys_of(pool, pos) for pos in T]
        ntr = trials if (synthetic or setting in ("prior", "sweep")) else trials // 10
        for tr in range(ntr):
            i, t = pairs[int(rng.integers(len(pairs)))]
            z = int(L.keys[t][i])
            yrep = pick_replacement(pool, pool_keys[t], z, rng, Yset)
            inj = Injector(L, t, i, rng)
            Bp0, Bp1, C0 = scenario_loads(L, i, t, yrep, inj.grow(0))
            plan = []                                   # (mode, eps, k0, k1)
            if setting == "sweep":
                for k in ksweep:
                    for mode, eps in (("exact", None), ("rounded", None)):
                        plan.append((mode, eps, k, k))
            else:
                for mode, eps in MODE_EPS:
                    if setting == "prior":
                        ks = []
                        for Bp in (Bp0, Bp1):
                            if mode == "exact":
                                U = Bp
                            elif mode == "rounded":
                                # leak l = ceil(log2 B) => B <= 2^l; k = 2^l suffices since the
                                # b=0 load max(k, C') stays in bucket l iff C' <= 2^l
                                U = (1 << clog2(Bp)) - 1
                            elif mode == "bound":
                                U = 0
                            else:
                                s = dp_shift(eps)
                                Bt = Bp + max(0, s + int(two_sided_geometric(eps, 1, rng)[0]))
                                U = Bt if mode == "dp" else (1 << clog2(Bt))
                            ks.append(k_from_upper(mode, U, eps, Bbar))
                        plan.append((mode, eps, ks[0], ks[1]))
                    else:
                        U = 2 * C_hat if setting == "shadow2" else C_hat
                        k = k_from_upper(mode, U, eps, Bbar)
                        plan.append((mode, eps, k, k))
            for mode, eps, k0, k1 in plan:
                B0, _, C = scenario_loads(L, i, t, yrep, inj.grow(k0))
                _, B1, _ = scenario_loads(L, i, t, yrep, inj.grow(k1))
                c0 = outcome(mode, eps, B0, 0, k0, Bbar, draws, rng)
                c1 = outcome(mode, eps, B1, 1, k1, Bbar, draws, rng)
                rows.append(dict(dataset=name, setting=setting if setting != "sweep" else f"sweep_k{k0}",
                                 instance=tag, n=meta["n"], trial=tr, mode=mode,
                                 eps="" if eps is None else eps, k=k0, k1=k1, C=C0,
                                 C_hat="" if C_hat is None else C_hat, B0=B0, B1=B1, Bbar=Bbar,
                                 correct0=c0, correct1=c1))
        print(f"{name} {setting} {tag}: done, {time.time() - t0:.0f}s", flush=True)
    if not rows:
        print(f"{name} {setting}: not applicable")
        return
    out = os.path.join(HERE, "results", f"{setting}_{name}.csv")
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)


if __name__ == "__main__":
    name, setting = sys.argv[1], sys.argv[2]
    trials = int(sys.argv[3]) if len(sys.argv) > 3 else 2000
    draws = int(sys.argv[4]) if len(sys.argv) > 4 else 100
    run(name, setting, trials, draws)
