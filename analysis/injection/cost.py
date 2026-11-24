"""Protocol cost of each leakage mode: probes per sender vector Q = B_revealed * T
(radius-0 covers: V(|S_t|,0) = 1, so sum_t V = T), relative to exact mode.

  exact      B
  rounded    2^ceil(log2 B)                (receiver pads to the power of two)
  bound      Bbar = 2 B                    (public bound with 2x headroom; any Bbar >= B)
  dp         E[B + max(0, s+Z)]            exact expectation over the two-sided geometric
  dprounded  E[2^ceil(log2(B + max(0, s+Z)))]

usage: cost.py   -> results/cost.csv
Synthetic rows average B over 10 independent draws of Y.
"""
import csv
import math
import os

import numpy as np

import datasets
from common import Loads, clog2, dp_shift

HERE = os.path.dirname(os.path.abspath(__file__))


def geo_pmf(eps, zmax):
    q = math.exp(-eps)
    z = np.arange(-zmax, zmax + 1)
    return z, (1 - q) / (1 + q) * q ** np.abs(z)


def expect_dp(B, eps, rounded):
    s = dp_shift(eps)
    z, p = geo_pmf(eps, int(60 / eps) + s)
    Bt = B + np.maximum(0, s + z)
    if rounded:
        vals = np.array([1 << clog2(int(v)) for v in Bt], dtype=float)
    else:
        vals = Bt.astype(float)
    return float((vals * p).sum() / p.sum())


def main():
    rows = []
    for name, cfg in datasets.CONFIGS.items():
        seeds = range(10) if cfg[0] == "uniform" else range(1)
        Bs = []
        for sd in seeds:
            Y, _, T, meta = datasets.get(name, sd)
            Bs.append(Loads(Y, T).B)
        nT = meta["T"]
        Bm = float(np.mean(Bs))
        revealed = dict(exact=Bm,
                        rounded=float(np.mean([1 << clog2(b) for b in Bs])),
                        bound=2 * Bm)
        for eps in (1.0, 0.1):
            revealed[f"dp(eps={eps})"] = float(np.mean([expect_dp(b, eps, False) for b in Bs]))
            revealed[f"dprounded(eps={eps})"] = float(np.mean([expect_dp(b, eps, True) for b in Bs]))
        for mode, Br in revealed.items():
            rows.append(dict(dataset=name, n=meta["n"], T=nT, keybits=meta["keybits"],
                             B_exact_mean=round(Bm, 2), B_values="/".join(map(str, sorted(set(Bs)))),
                             mode=mode, B_revealed=round(Br, 2), Q=round(Br * nT, 1),
                             Q_over_exact=round(Br / Bm, 3)))
    out = os.path.join(HERE, "results", "cost.csv")
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    for r in rows:
        print(r)


if __name__ == "__main__":
    main()
