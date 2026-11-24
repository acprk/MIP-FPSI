"""Summarise results/known_*.csv and results/prior_*.csv into results/summary_*.csv.

usage: summarize.py PREFIX   (PREFIX = known | prior | shadow ...)
Advantage = Pr[correct | b=1] + Pr[correct | b=0] - 1 = 2 Pr[correct] - 1 for uniform b,
averaged over trials (random target); 95% CI from the per-trial spread.
"""
import csv
import glob
import math
import os
import sys
from collections import defaultdict

import numpy as np

from common import DELTA_DP, dp_tv

HERE = os.path.dirname(os.path.abspath(__file__))


def main(prefix):
    groups = defaultdict(list)
    order = []
    for path in sorted(glob.glob(os.path.join(HERE, "results", f"{prefix}_*.csv"))):
        if os.path.basename(path).startswith("summary_"):
            continue
        with open(path) as f:
            for r in csv.DictReader(f):
                key = (r["dataset"], r.get("setting", prefix), r["mode"], r["eps"])
                if key not in groups:
                    order.append(key)
                groups[key].append(r)
    out_rows = []
    for key in order:
        rs = groups[key]
        a = np.array([float(r["correct0"]) + float(r["correct1"]) - 1 for r in rs])
        k = np.array([int(r["k"]) for r in rs])
        C = np.array([int(r["C"]) for r in rs])
        eps = key[3]
        row = dict(dataset=key[0], setting=key[1], mode=key[2], eps=eps, trials=len(rs),
                   adv=round(float(a.mean()), 4),
                   ci95=round(float(1.96 * a.std(ddof=1) / math.sqrt(len(a))), 4),
                   k_median=int(np.median(k)), k_mean=round(float(k.mean()), 1), k_max=int(k.max()),
                   C_median=int(np.median(C)), C_max=int(C.max()))
        if rs[0].get("C_hat"):
            ch = np.array([int(r["C_hat"]) for r in rs])
            row.update(C_hat_median=int(np.median(ch)), C_hat_min=int(ch.min()),
                       frac_k_too_small=round(float(np.mean(k <= C)), 4))
        if eps:
            e = float(eps)
            tv, clip = dp_tv(e)
            row.update(adv_opt_theory=round(tv + clip, 4), bound_e_eps=round(math.exp(e) - 1 + DELTA_DP, 4))
        else:
            row.update(adv_opt_theory="", bound_e_eps="")
        out_rows.append(row)
    out = os.path.join(HERE, "results", f"summary_{prefix}.csv")
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(out_rows[0].keys()))
        w.writeheader()
        w.writerows(out_rows)
    for r in out_rows:
        print(r)


if __name__ == "__main__":
    main(sys.argv[1])
