"""Generate the LaTeX tables of Sec. VII from benchmark logs (no hand-copied numbers).
usage: python3 make_tables.py <final_suite_dir> <gao_pinned_dir>  -> tables/{cmp,scale,real,net}.tex
Each configuration may have several repetitions (<name>_rep<k>.log); the median CPU time is used
(communication is deterministic up to a few bytes)."""
import glob
import os
import re
import statistics
import sys

suite, gao = sys.argv[1], sys.argv[2]
os.makedirs("tables", exist_ok=True)
ROUNDS = 60  # upper bound on sequential rounds of our protocol (Sec. VII-A)


def parse(p):
    t = open(p).read()
    head = re.search(r"^m=(\d+) n=(\d+).*?B=(\d+) U=(\d+) Q=(\d+) bins=(\d+) prog=(\d+) tau=(\d+) fallback=(\d)", t, re.M)
    tm = re.search(r"time_s=([\d.]+) cpu_s\(both parties\)=([\d.]+) comm_MB=([\d.]+)", t)
    ok = re.search(r"matches=(\d+) truth=(\d+) false_neg=(\d+) false_pos=(\d+)", t)
    if not (tm and ok):
        return None
    d = dict(cpu=float(tm[2]), mb=float(tm[3]), match=int(ok[1]), truth=int(ok[2]), fn=int(ok[3]), fp=int(ok[4]))
    if head:
        d.update(m=int(head[1]), n=int(head[2]), B=int(head[3]), U=int(head[4]), Q=int(head[5]),
                 bins=int(head[6]), tau=int(head[8]), fb=int(head[9]))
    return d


def ours(name):
    runs = [r for r in (parse(p) for p in sorted(glob.glob(os.path.join(suite, name + "_rep*.log")))) if r]
    if not runs:
        return None
    med = dict(runs[0])
    med["cpu"] = statistics.median(r["cpu"] for r in runs)
    med["mb"] = statistics.median(r["mb"] for r in runs)
    med["reps"] = len(runs)
    med["fn"] = sum(r["fn"] for r in runs)
    med["fp"] = sum(r["fp"] for r in runs)
    return med


def gao_log(lg):
    p = f"{gao}/gao_m2e{lg}_n2e{lg}.log"
    if not os.path.exists(p):
        return None
    t = open(p).read()
    off = re.search(r"Offline done\s+([\d.]+)", t)
    on = re.search(r"online done\s+[\d.]+\s+([\d.]+)", t)
    mb = re.search(r"comm total:\s+([\d.]+)MB", t)
    if off and on and mb:
        return dict(off=float(off[1]) / 1e3, on=float(on[1]) / 1e3, mb=float(mb[1]))
    return None


def f(x):
    if x is None:
        return "--"
    return f"{x:.2f}" if x < 10 else (f"{x:.1f}" if x < 100 else f"{x:,.0f}".replace(",", "{,}"))


# Table II: comparison with Gao and all pairs (d=128, delta=4)
rows = []
for lg in (8, 10, 12):
    o, a, g = ours(f"bal_d128_2e{lg}"), ours(f"allpairs_d128_2e{lg}"), gao_log(lg)
    rows.append(f"$2^{{{lg}}}$ & {f(g['on']) if g else '--'} & {f(g['off']) if g else '--'} & {f(g['mb']) if g else '--'}"
                f" & {f(a['cpu']) if a else '--'} & {f(a['mb']) if a else '--'}"
                f" & \\textbf{{{f(o['cpu']) if o else '--'}}} & \\textbf{{{f(o['mb']) if o else '--'}}} \\\\")
open("tables/cmp.tex", "w").write("\n".join(rows) + "\n")

# Table III: scaling and parameter variants
rows = []
def srow(name, m, n, d, dl, bs, note=""):
    o = ours(name)
    if o:
        rows.append(f"{m} & {n} & {d} & {dl} & {bs}{note} & {o['B']}/{o['U']} & {o['Q']} & {f(o['cpu'])} & {f(o['mb'])} & {o['fn']}/{o['fp']} \\\\")
for lg in (8, 10, 12, 14, 16):
    srow(f"bal_d128_2e{lg}", f"$2^{{{lg}}}$", f"$2^{{{lg}}}$", 128, 4, "(5,1)")
srow("pad_d128_2e12", "$2^{12}$", "$2^{12}$", 128, 4, "(5,1)", "$^{p}$")
rows.append("\\midrule")
srow("d64_delta3_b4s1_2e12", "$2^{12}$", "$2^{12}$", 64, 3, "(4,1)")
srow("d64_delta3_b6s3_2e12", "$2^{12}$", "$2^{12}$", 64, 3, "(6,3)")
srow("d256_delta8_b9s1_2e12", "$2^{12}$", "$2^{12}$", 256, 8, "(9,1)")
rows.append("\\midrule")
for lm in (8, 10):
    for ln in (12, 16):
        srow(f"unb_m2e{lm}_n2e{ln}_b5", f"$2^{{{lm}}}$", f"$2^{{{ln}}}$", 128, 4, "(5,1)")
        srow(f"unb_m2e{lm}_n2e{ln}_b3", f"$2^{{{lm}}}$", f"$2^{{{ln}}}$", 128, 4, "(3,1)", "$^{r}$")
open("tables/scale.tex", "w").write("\n".join(rows) + "\n")

# Table IV: real data
rows = []
code, cif, apc = ours("code_simhash64"), ours("cifar_mild"), ours("allpairs_code")
if code:
    rows.append(f"CPython & 2{{,}}973 & 2{{,}}959 & 3 & (4,1) & {code['B']}/{code['U']} & {code['Q']} & {code['match']} & {f(code['cpu'])} & {f(code['mb'])} & {f(apc['cpu']) if apc else '--'} \\\\")
if cif:
    est = None
    if apc:
        est = apc["cpu"] * (9999 * 54994) / (2973 * 2959)
    rows.append(f"CIFAR-10 & 9{{,}}999 & 54{{,}}994 & 4 & (7,3) & {cif['B']}/{cif['U']} & {cif['Q']} & {cif['match']} & {f(cif['cpu'])} & {f(cif['mb'])} & {f(est) + '$^e$' if est else '--'} \\\\")
open("tables/real.tex", "w").write("\n".join(rows) + "\n")

# Table: network estimates at 2^12 (CPU + bytes/bandwidth + rounds*RTT); Gao: online CPU + its bytes,
# zero rounds (favourable to Gao)
rows = []
o, g = ours("bal_d128_2e12"), gao_log(12)
for label, bw, rtt in (("LAN (1\\,Gbps, 0.2\\,ms)", 1e9, 0.0002), ("WAN (100\\,Mbps, 80\\,ms)", 1e8, 0.08)):
    to = o["cpu"] + o["mb"] * 8 * 2**20 / bw + ROUNDS * rtt if o else None
    tg = g["on"] + g["mb"] * 8 * 2**20 / bw if g else None
    rows.append(f"{label} & {f(tg)} & {f(to)} \\\\")
open("tables/net.tex", "w").write("\n".join(rows) + "\n")
for k in ("cmp", "scale", "real", "net"):
    print(f"--- {k}\n" + open(f"tables/{k}.tex").read())
