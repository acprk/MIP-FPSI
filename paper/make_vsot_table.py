"""Table for the same-host comparison with otFPSI (xDup).
usage: python3 make_vsot_table.py <vs_otfpsi_dir>  -> tables/vsot.tex
Times from /usr/bin/time (%e wall, %U user, %S sys); medians over repetitions.
otFPSI communication from its 'Total Communication: N bytes' line; ours from comm_MB (MiB)."""
import glob
import os
import re
import statistics
import sys

d = sys.argv[1]
os.makedirs("tables", exist_ok=True)


def runs(prefix):
    out = []
    for t in sorted(glob.glob(os.path.join(d, prefix + "_rep*.time"))):
        parts = open(t).read().split()
        if len(parts) < 3:
            continue  # run still in progress
        w, u, s = map(float, parts[:3])
        log = open(t[:-5] + ".log").read()
        m = re.search(r"Total Communication: (\d+) bytes", log)
        mb = int(m[1]) / 2**20 if m else None
        m2 = re.search(r"comm_MB=([\d.]+)", log)
        if m2:
            mb = float(m2[1])
        out.append((w, u + s, mb))
    return out


def med(rs, k):
    vals = [r[k] for r in rs if r[k] is not None]
    return statistics.median(vals) if vals else None


def f(x):
    if x is None:
        return "--"
    return f"{x:.2f}" if x < 10 else (f"{x:.1f}" if x < 100 else f"{x:,.0f}".replace(",", "{,}"))


rows = []
for delta in (4, 16):
    for lg in (8, 10, 12, 14):
        o = runs(f"otfpsi_d128_delta{delta}_2e{lg}")
        u = runs(f"ours_d128_delta{delta}_2e{lg}")
        if not o or not u:
            continue
        ow, oc, om = med(o, 0), med(o, 1), med(o, 2)
        uw, uc, um = med(u, 0), med(u, 1), med(u, 2)
        rows.append(f"{delta} & $2^{{{lg}}}$ & {f(ow)} & {f(oc)} & {f(om)} & \\textbf{{{f(uw)}}} & {f(um)} & {ow / uw:.1f}$\\times$ \\\\")
    rows.append("\\midrule")
if rows and rows[-1] == "\\midrule":
    rows.pop()
open("tables/vsot.tex", "w").write("\n".join(rows) + "\n")
print(open("tables/vsot.tex").read())
