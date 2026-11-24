#!/usr/bin/env python3
"""Aggregate experiments_r6 runs: writes results.csv (all attempts) and tables_*.md per experiment.
Usage: aggregate.py e1|e2
Artifact note: the only change from the version used in the campaign is that the experiments_r6
directory is taken from $R6 (default: the directory of this script) instead of an absolute path."""
import csv, json, os, statistics as st, sys
from collections import defaultdict

R6 = os.environ.get("R6", os.path.dirname(os.path.abspath(__file__)))
COLS = ["tag", "exp", "kind", "delta", "stage", "mode", "seed", "round", "rep", "attempt", "final",
        "throttled_excluded", "all_attempts_throttled", "start", "end", "rc", "correct",
        "false_neg", "false_pos", "matches", "truth", "B", "U", "Q", "T", "bins", "tau", "fallback",
        "proto_cpu_s", "cpu_distance_s", "cpu_threshold_s", "cpu_total_s", "user_s", "sys_s",
        "proto_wall_s", "wall_s", "comm_MiB", "maxrss_kb", "load_before", "load_after",
        "thr_before", "thr_after", "thr_delta", "thr_per_min", "gate_wait_s", "verif_ok", "cmd"]


def load(sub):
    with open(f"{R6}/{sub}/runs/summary.jsonl") as f:
        recs = [json.loads(l) for l in f if l.strip()]
    with open(f"{R6}/{sub}/results.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLS, extrasaction="ignore")
        w.writeheader()
        for r in recs:
            r = dict(r)
            r["cmd"] = " ".join(r["cmd"])
            w.writerow(r)
    return [r for r in recs if r.get("final")]


def med(xs):
    return st.median(xs) if xs else float("nan")


def q(xs, p):
    xs = sorted(xs)
    if len(xs) == 1:
        return xs[0]
    k = (len(xs) - 1) * p
    lo = int(k)
    hi = min(lo + 1, len(xs) - 1)
    return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)


def f(x, nd=2):
    if x != x:
        return "-"
    if abs(x) >= 1000:
        return f"{x:,.0f}"
    if abs(x) >= 100:
        return f"{x:.0f}"
    if abs(x) >= 10:
        return f"{x:.1f}"
    return f"{x:.{nd}f}"


def mr(xs, nd=2):
    return f"{f(med(xs), nd)} [{f(min(xs), nd)}–{f(max(xs), nd)}]" if xs else "-"


def e1():
    recs = load("e1_attribution")
    by = defaultdict(list)
    for r in recs:
        by[(r["delta"], r["stage"])].append(r)
    out = []
    out.append("| δ | stage | n | proto CPU s | proc CPU s | wall s | comm MiB | peak RSS MiB | B/U/Q | fallback | FN/FP all 0 |")
    out.append("|---|---|---|---|---|---|---|---|---|---|---|")
    for (d, s) in sorted(by):
        rs = by[(d, s)]
        bq = sorted({f"{r.get('B')}/{r.get('U')}/{r.get('Q')}" for r in rs})
        out.append(f"| {d} | {s} | {len(rs)} | {mr([r['proto_cpu_s'] for r in rs])} | {mr([r['cpu_total_s'] for r in rs])} | "
                   f"{mr([r['proto_wall_s'] for r in rs])} | {mr([r['comm_MiB'] for r in rs])} | "
                   f"{mr([r['maxrss_kb'] / 1024 for r in rs], 0)} | {','.join(bq)} | "
                   f"{sorted({r.get('fallback') for r in rs})} | {all(r['correct'] for r in rs)} |")
    out.append("")
    out.append("Paired ratios stage(k)/stage(4) within the same round (median [min–max] over rounds):")
    out.append("")
    out.append("| δ | stage | proto CPU | proc CPU | wall | comm | peak RSS |")
    out.append("|---|---|---|---|---|---|---|")
    for (d, s) in sorted(by):
        if s == "s4_compressed":
            continue
        ref = {r["round"]: r for r in by[(d, "s4_compressed")]}
        rat = defaultdict(list)
        for r in by[(d, s)]:
            b = ref.get(r["round"])
            if not b:
                continue
            for k in ("proto_cpu_s", "cpu_total_s", "proto_wall_s", "comm_MiB", "maxrss_kb"):
                rat[k].append(r[k] / b[k])
        out.append(f"| {d} | {s} | " + " | ".join(mr(rat[k]) for k in ("proto_cpu_s", "cpu_total_s", "proto_wall_s", "comm_MiB", "maxrss_kb")) + " |")
    # same-binary check: stage 2 against stage 4 run with the same fpsi_bench_kc binary
    for d in (4, 8):
        ref = {r["round"]: r for r in by[(d, "s4kc_harnesscheck")]}
        rat = defaultdict(list)
        for r in by[(d, "s2_full_keycheck")]:
            b = ref.get(r["round"])
            if b:
                for k in ("proto_cpu_s", "cpu_total_s", "proto_wall_s", "comm_MiB", "maxrss_kb"):
                    rat[k].append(r[k] / b[k])
        out.append(f"| {d} | s2 / s4kc (same binary) | " + " | ".join(mr(rat[k]) for k in ("proto_cpu_s", "cpu_total_s", "proto_wall_s", "comm_MiB", "maxrss_kb")) + " |")
    txt = "\n".join(out)
    with open(f"{R6}/e1_attribution/tables.md", "w") as fo:
        fo.write(txt + "\n")
    print(txt)


def e2():
    recs = load("e2_paired")
    ours = defaultdict(list)
    ot = {}
    for r in recs:
        if r["kind"] == "otfpsi":
            ot[(r["seed"], r["mode"])] = r
        else:
            ours[(r["delta"], r["mode"])].append(r)
    out = ["Native otFPSI (in-process transport), one run per seed and OT mode:", "",
           "| seed | mode | proto CPU s | proc CPU s | proto wall s | wall s | comm MiB | peak RSS MiB | thr/min |",
           "|---|---|---|---|---|---|---|---|---|"]
    for (s, m) in sorted(ot):
        r = ot[(s, m)]
        out.append(f"| {s} | {m} | {f(r.get('proto_cpu_s', float('nan')))} | {f(r['cpu_total_s'])} | {f(r.get('proto_wall_s', float('nan')))} | "
                   f"{f(r['wall_s'])} | {f(r.get('comm_MiB', float('nan')))} | {f(r['maxrss_kb'] / 1024, 0)} | {f(r['thr_per_min'], 0)} |")
    out += ["", "Ours, per δ and mode, all 15 runs (5 seeds × 3 reps): median and IQR", "",
            "| δ | mode | n | proto CPU s med [IQR] | proc CPU s med [IQR] | wall s med | comm MiB med [min–max] | peak RSS MiB med | all FN=FP=0 |",
            "|---|---|---|---|---|---|---|---|---|"]
    for (d, m) in sorted(ours):
        rs = ours[(d, m)]
        pc = [r["proto_cpu_s"] for r in rs]
        tc = [r["cpu_total_s"] for r in rs]
        out.append(f"| {d} | {m} | {len(rs)} | {f(med(pc))} [{f(q(pc, .25))}–{f(q(pc, .75))}] | {f(med(tc))} [{f(q(tc, .25))}–{f(q(tc, .75))}] | "
                   f"{f(med([r['proto_wall_s'] for r in rs]))} | {mr([r['comm_MiB'] for r in rs])} | {f(med([r['maxrss_kb'] / 1024 for r in rs]), 0)} | "
                   f"{all(r['correct'] for r in rs)} |")
    for otm in ("otfpsi_softspoken", "otfpsi_silent"):
        for key, lab in (("proto_cpu_s", "protocol-scoped CPU (both tools)"), ("cpu_total_s", "whole-process CPU (both tools)")):
            out += ["", f"Per-seed paired ratio {otm} / ours ({lab}); ours = median of the seed's 3 reps", "",
                    "| δ | mode | s1 | s2 | s3 | s4 | s5 | median [min–max] over seeds |", "|---|---|---|---|---|---|---|---|"]
            for (d, m) in sorted(ours):
                rats = []
                cells = []
                for s in range(1, 6):
                    o = ot.get((s, otm))
                    rs = [r[key] for r in ours[(d, m)] if r["seed"] == s]
                    if o and rs and o.get(key):
                        x = o[key] / med(rs)
                        rats.append(x)
                        cells.append(f"{x:.1f}")
                    else:
                        cells.append("-")
                out.append(f"| {d} | {m} | " + " | ".join(cells) + f" | {mr(rats, 1)} |")
    out += ["", "B/Q per seed (exact mode; rounded and U-free in parentheses where different):", "",
            "| δ | seed | exact B/U/Q | pow2 B/U/Q | U-free B/U/Q |", "|---|---|---|---|---|"]
    for d in (4, 8):
        for s in range(1, 6):
            cells = []
            for m in ("exact", "pow2", "Ufree"):
                v = sorted({f"{r.get('B')}/{r.get('U')}/{r.get('Q')}" for r in ours[(d, m)] if r["seed"] == s})
                cells.append(",".join(v))
            out.append(f"| {d} | {s} | " + " | ".join(cells) + " |")
    txt = "\n".join(out)
    with open(f"{R6}/e2_paired/tables.md", "w") as fo:
        fo.write(txt + "\n")
    print(txt)


if __name__ == "__main__":
    {"e1": e1, "e2": e2}[sys.argv[1]]()
