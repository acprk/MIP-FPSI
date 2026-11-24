#!/usr/bin/env python3
"""Aggregate runner summaries into markdown tables (median [min-max] over repetitions).
Usage: aggregate.py A|B SUMMARY.jsonl [RUNSDIR]"""
import json, re, statistics, sys
from collections import defaultdict


def load(path):
    with open(path) as f:
        return [json.loads(l) for l in f if l.strip()]


def key(tag):
    return re.sub(r"_r\d+$", "", tag)


def med(vals):
    vals = [v for v in vals if v is not None]
    if not vals:
        return None, None, None
    return statistics.median(vals), min(vals), max(vals)


def fmt(t, nd=2):
    m, lo, hi = t
    if m is None:
        return "-"
    if lo == hi:
        return f"{m:.{nd}f}"
    return f"{m:.{nd}f} [{lo:.{nd}f}-{hi:.{nd}f}]"


def table_A(recs):
    g = defaultdict(list)
    for r in recs:
        if r.get("skipped"):
            continue
        g[key(r["tag"])].append(r)
    print("| config | n | online wall s | online CPU s | total wall s | total CPU s (user+sys) | comm MiB | max RSS MB | check |")
    print("|---|---|---|---|---|---|---|---|---|")
    for k in sorted(g, key=lambda s: (s.split("_")[0], s)):
        rs = g[k]
        if rs[0]["kind"] == "gao":
            chk = ",".join(sorted({str(r.get("intersection")) for r in rs}))
            print(f"| {k} | {len(rs)} | {fmt(med([r.get('online_wall_s') for r in rs]))} | "
                  f"{fmt(med([r.get('online_cpu_s') for r in rs]))} | {fmt(med([r.get('total_wall_s') for r in rs]),1)} | "
                  f"{fmt(med([r.get('cpu_total_s') for r in rs]),1)} | {fmt(med([r.get('comm_MiB') for r in rs]),1)} | "
                  f"{fmt(med([r.get('maxrss_kb', 0)/1024 for r in rs]),0)} | |I|={chk} (expect 6) |")
        else:
            chk = "FN=FP=0" if all(r.get("false_neg") == 0 and r.get("false_pos") == 0 for r in rs) else "ERR"
            print(f"| {k} | {len(rs)} | {fmt(med([r.get('proto_wall_s') for r in rs]),3)} | "
                  f"{fmt(med([r.get('proto_cpu_s') for r in rs]),3)} | (same) | "
                  f"{fmt(med([r.get('cpu_total_s') for r in rs]),3)} | {fmt(med([r.get('comm_MiB') for r in rs]),2)} | "
                  f"{fmt(med([r.get('maxrss_kb', 0)/1024 for r in rs]),0)} | {chk}, B={sorted({r.get('B') for r in rs})} |")
    skipped = [r for r in recs if r.get("skipped")]
    for r in skipped:
        print(f"\nskipped {r['tag']}: {r['reason']}")
    # paired ratios: same repetition index
    print("\nPaired ratios Gao / ours (per repetition, then median [min-max]):\n")
    print("| setting | Gao basis | ours mode | online-CPU ratio | total-CPU ratio | comm ratio |")
    print("|---|---|---|---|---|---|")
    by = {r["tag"]: r for r in recs if not r.get("skipped")}
    pairs = [("gao_d4_2e8", "d4_2e8"), ("gao_d4_2e10", "d4_2e10"), ("gao_d4_2e12", "d4_2e12"),
             ("gao_d8_2e7", "d8_2e7"), ("gaoP_d8_2e8", "d8_2e8"), ("gaoP_d8_2e10", "d8_2e10"),
             ("gaoP_d8_2e12", "d8_2e12")]
    for gk, ok in pairs:
        for mode in ("exact", "pow2"):
            on, to, cm = [], [], []
            for k in (1, 2, 3):
                g_ = by.get(f"{gk}_r{k}")
                o_ = by.get(f"ours_{mode}_{ok}_r{k}")
                if not g_ or not o_:
                    continue
                if g_.get("online_cpu_s") is not None:
                    on.append(g_["online_cpu_s"] / o_["proto_cpu_s"])
                to.append(g_["cpu_total_s"] / o_["proto_cpu_s"])
                cm.append(g_["comm_MiB"] / o_["comm_MiB"])
            if to:
                print(f"| {ok} | {gk} | {mode} | {fmt(med(on),1)} | {fmt(med(to),0)} | {fmt(med(cm),1)} |")


def parse_flash(runsdir, tag):
    with open(f"{runsdir}/{tag}.log") as f:
        t = f.read()
    d = {}
    m = re.search(r"total duration: ([\d.]+)(ms|s|µs)", t)
    if m:
        d["wall"] = float(m.group(1)) / (1000 if m.group(2) == "ms" else 1)
    m = re.search(r"communication: ([\d.]+) KB", t)
    if m:
        d["comm_MiB"] = float(m.group(1)) / 1024
    m = re.search(r"fpr: ([\d.eE+-]+)", t)
    if m:
        d["fpr"] = float(m.group(1))
    return d


def table_B(recs, runsdir):
    g = defaultdict(list)
    for r in recs:
        if r["kind"] == "generic":
            r.update(parse_flash(runsdir, r["tag"]))
        g[key(r["tag"])].append(r)
    print("| config | n | online/protocol wall s | process CPU s (user+sys) | comm MiB | max RSS MB | check |")
    print("|---|---|---|---|---|---|---|")
    for k in sorted(g):
        rs = g[k]
        if rs[0]["kind"] == "generic":
            chk = f"rc={sorted({r['rc'] for r in rs})}, fpr={sorted({r.get('fpr') for r in rs})}"
            w = med([r.get("wall") for r in rs])
        else:
            chk = (f"truth={sorted({r.get('truth') for r in rs})}, FN={sorted({r.get('false_neg') for r in rs})}, "
                   f"FP={sorted({r.get('false_pos') for r in rs})}, fallback={sorted({r.get('fallback') for r in rs})}")
            w = med([r.get("proto_wall_s") for r in rs])
        print(f"| {k} | {len(rs)} | {fmt(w,3)} | {fmt(med([r.get('user_s',0)+r.get('sys_s',0) for r in rs]),2)} | "
              f"{fmt(med([r.get('comm_MiB') for r in rs]),2)} | {fmt(med([r.get('maxrss_kb',0)/1024 for r in rs]),0)} | {chk} |")


if __name__ == "__main__":
    which, path = sys.argv[1], sys.argv[2]
    recs = load(path)
    if which == "A":
        table_A(recs)
    else:
        table_B(recs, sys.argv[3])
