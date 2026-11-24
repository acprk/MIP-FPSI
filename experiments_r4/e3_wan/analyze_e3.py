"""Analysis of E3 (run_e3.sh): wall-clock time under emulated networks (median and range of 3 runs).
Ours: receiver wall time of the protocol (net_bench); otFPSI: process elapsed time (/usr/bin/time),
which additionally includes its input generation and plaintext checks (about 1% of its CPU time).
usage: python3 analyze_e3.py <results_dir>"""
import os
import re
import statistics as st
import sys
from collections import defaultdict

D = sys.argv[1]
groups = defaultdict(list)
problems = []
for f in sorted(os.listdir(D)):
    if f.endswith(".recv.log"):
        tag = f[:-9]
        t = open(os.path.join(D, f)).read()
        m = re.search(r"wall_s=([\d.]+)", t)
        if not m or "false_neg=0 false_pos=0" not in t:
            problems.append(tag)
            continue
        groups[re.sub(r"_r\d+$", "", tag)].append(float(m[1]))
    elif f.endswith(".time"):
        tag = f[:-5]
        log = open(os.path.join(D, tag + ".log")).read()
        if log.count("Verification succeeded") < 2:
            problems.append(tag)
            continue
        groups[re.sub(r"_r\d+$", "", tag)].append(float(open(os.path.join(D, f)).read().split()[0]))
for g, v in sorted(groups.items()):
    print(f"{g:22s} n={len(v)} wall median {st.median(v):8.1f} s  [{min(v):.1f}, {max(v):.1f}]")
print("PROBLEMS: " + " ".join(problems) if problems else "all checks passed")
sys.exit(1 if problems else 0)
