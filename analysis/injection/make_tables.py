"""Markdown tables for RESULTS.md from results/summary_*.csv, cost.csv, *.meta.txt.

usage: make_tables.py -> results/tables.md
"""
import ast
import csv
import glob
import os
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.join(HERE, "results")
DS = ["U128-n2^10", "U128-n2^12", "U64-n2^12", "code-simhash64", "cifar-dhash64-mild"]
COLS = [("exact", ""), ("rounded", ""), ("bound", ""), ("dp", "1.0"), ("dp", "0.1"),
        ("dprounded", "1.0"), ("dprounded", "0.1")]
CNAME = {("exact", ""): "exact", ("rounded", ""): "rounded", ("bound", ""): "bound 2B",
         ("dp", "1.0"): "DP e=1", ("dp", "0.1"): "DP e=0.1", ("dprounded", "1.0"): "DP+rnd e=1",
         ("dprounded", "0.1"): "DP+rnd e=0.1"}
COST = {("exact", ""): "exact", ("rounded", ""): "rounded", ("bound", ""): "bound",
        ("dp", "1.0"): "dp(eps=1.0)", ("dp", "0.1"): "dp(eps=0.1)",
        ("dprounded", "1.0"): "dprounded(eps=1.0)", ("dprounded", "0.1"): "dprounded(eps=0.1)"}


def read(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def idx(rows):
    return {(r["dataset"], r["mode"], r["eps"]): r for r in rows}


def main():
    out = []
    meta = {}
    for p in glob.glob(os.path.join(R, "known_*.meta.txt")):
        with open(p) as f:
            m = ast.literal_eval(f.read())
        meta[m["dataset"]] = m
    out.append("### Datasets\n")
    out.append("| dataset | d | delta | cover (b,s) | key bits | T | n | B(Y) | load-1 (y,t) pairs |")
    out.append("|---|---|---|---|---|---|---|---|---|")
    for d in DS:
        m = meta[d]
        out.append(f"| {d} | {m['d']} | {m['delta']} | ({m['b']},{m['s']}) | {m['keybits']} | {m['T']} | "
                   f"{m['n']} | {m['B']} | {100 * m['load1_frac']:.1f}% |")
    known = idx(read(os.path.join(R, "summary_known.csv")))
    cost = {(r["dataset"], r["mode"]): r for r in read(os.path.join(R, "cost.csv"))}
    out.append("\n### Table A: knowledge (a), advantage [95% CI half-width] / median k / Q overhead\n")
    out.append("| dataset | C (med/max) | " + " | ".join(CNAME[c] for c in COLS) + " |")
    out.append("|---|---|" + "---|" * len(COLS))
    for d in DS:
        r0 = known[(d, "exact", "")]
        cells = []
        for mode, eps in COLS:
            r = known[(d, mode, eps)]
            q = cost[(d, COST[(mode, eps)])]["Q_over_exact"]
            cells.append(f"{float(r['adv']):.3f} [{float(r['ci95']):.3f}] / k={r['k_median']} / {float(q):.2f}x")
        out.append(f"| {d} | {r0['C_median']}/{r0['C_max']} | " + " | ".join(cells) + " |")
    out.append("\nDP reference: optimal advantage (1-q)/(1+q)+clip = tanh(eps/2) = "
               f"{known[(DS[0], 'dp', '1.0')]['adv_opt_theory']} (eps=1), "
               f"{known[(DS[0], 'dp', '0.1')]['adv_opt_theory']} (eps=0.1); paper bound e^eps-1+delta = "
               f"{known[(DS[0], 'dp', '1.0')]['bound_e_eps']}, {known[(DS[0], 'dp', '0.1')]['bound_e_eps']}.")
    # knowledge settings
    out.append("\n### Table B: advantage without exact background knowledge (median k in parentheses)\n")
    sets = [("known", "(a) exact"), ("prior", "(c) prior run"), ("shadow", "(b) shadow"),
            ("shadow2", "(b) shadow x2"), ("uniform", "(b') uniform prior")]
    tabs = {}
    for s, _ in sets:
        p = os.path.join(R, f"summary_{s}.csv")
        tabs[s] = idx(read(p)) if os.path.exists(p) else {}
    out.append("| dataset | setting | " + " | ".join(CNAME[c] for c in COLS) + " |")
    out.append("|---|---|" + "---|" * len(COLS))
    for d in DS:
        for s, lab in sets:
            if (d, "exact", "") not in tabs[s]:
                continue
            cells = []
            for mode, eps in COLS:
                r = tabs[s][(d, mode, eps)]
                cells.append(f"{float(r['adv']):.3f} ({r['k_median']})")
            out.append(f"| {d} | {lab} | " + " | ".join(cells) + " |")
    # sweep
    sw = read(os.path.join(R, "summary_sweep.csv"))
    by = defaultdict(dict)
    for r in sw:
        k = int(r["setting"].split("_k")[1])
        by[(r["dataset"], r["mode"])][k] = float(r["adv"])
    out.append("\n### Table C: blind fixed-k sweep (no knowledge): smallest k with advantage >= 0.99\n")
    out.append("| dataset | max competing load C (med/max) | exact: min k | exact adv at min k - 1 | "
               "exact: all k >= min k work? | rounded: k in grid that work |")
    out.append("|---|---|---|---|---|---|")
    for d in DS:
        r0 = known[(d, "exact", "")]
        ex = by[(d, "exact")]
        kmin = min(k for k, a in ex.items() if a >= 0.99)
        below = ex.get(kmin - 1, "-")
        mono = all(a >= 0.99 for k, a in ex.items() if k >= kmin)
        rk = sorted(k for k, a in by[(d, "rounded")].items() if a >= 0.99)
        out.append(f"| {d} | {r0['C_median']}/{r0['C_max']} | {kmin} | {below} | {'yes' if mono else 'no'} | "
                   f"{', '.join(map(str, rk))} |")
    # cost
    out.append("\n### Table D: probes per sender vector Q = B_revealed * T, ratio to exact\n")
    modes = ["exact", "rounded", "bound", "dp(eps=1.0)", "dprounded(eps=1.0)", "dp(eps=0.1)",
             "dprounded(eps=0.1)"]
    out.append("| dataset | T | B | " + " | ".join(modes) + " |")
    out.append("|---|---|---|" + "---|" * len(modes))
    for d in DS:
        cells = [f"{cost[(d, m)]['Q']} ({float(cost[(d, m)]['Q_over_exact']):.2f}x)" for m in modes]
        c0 = cost[(d, "exact")]
        out.append(f"| {d} | {c0['T']} | {c0['B_exact_mean']} | " + " | ".join(cells) + " |")
    with open(os.path.join(R, "tables.md"), "w") as f:
        f.write("\n".join(out) + "\n")
    print("\n".join(out))


if __name__ == "__main__":
    main()
