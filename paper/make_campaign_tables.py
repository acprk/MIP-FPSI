"""All evaluation tables from ONE campaign directory (scripts/run_campaign2.sh).
usage: python3 make_campaign_tables.py <campaign_dir>  -> tables/c2_*.tex, prints a summary
Ours: two processes; wall = receiver wall (median over reps), CPU = sender + receiver cpu_s,
communication = bytes sent by both parties in MiB. Every run of ours is checked: FN/FP must be 0.
otFPSI: /usr/bin/time %e %U %S of one process with two threads; CPU = user + sys;
communication from 'Total Communication: N bytes' (MiB)."""
import glob
import os
import re
import statistics
import sys

D = sys.argv[1]
os.makedirs("tables", exist_ok=True)
problems = []


def kv(text):
    return dict(re.findall(r"(\w+)=([\w.]+)", text))


def ours(tag):
    """List of dicts for runs tag_r1.. (or tag itself)."""
    out = []
    files = sorted(glob.glob(os.path.join(D, tag + "_r*.recv.log"))) or glob.glob(os.path.join(D, tag + ".recv.log"))
    for rf in files:
        sf = rf.replace(".recv.log", ".send.log")
        r, s = open(rf).read(), open(sf).read() if os.path.exists(sf) else ""
        if "wall_s" not in r or "wall_s" not in s:
            problems.append(f"{rf}: incomplete run")
            continue
        a, b = kv(r), kv(s)
        # The collision-dropping (UniqC-style) control is lossy by design and is reported as such.
        lossy_control = "_uniqc" in os.path.basename(rf)
        if not lossy_control and (a.get("false_neg") != "0" or a.get("false_pos") != "0"):
            problems.append(f"{rf}: FN/FP {a.get('false_neg')}/{a.get('false_pos')}")
        out.append(dict(wall=float(a["wall_s"]), cpu=float(a["cpu_s"]) + float(b["cpu_s"]),
                        mb=float(a["sent_MB"]) + float(a["recv_MB"]), B=int(a.get("B", 0)),
                        U=int(a.get("U", 0)), Q=int(a.get("Q", 0)), fb=a.get("fallback", "0"),
                        matches=int(a.get("matches", -1))))
    return out


def otf(tag):
    out = []
    files = sorted(glob.glob(os.path.join(D, tag + "_r*.time"))) or glob.glob(os.path.join(D, tag + ".time"))
    for t in files:
        p = open(t).read().split()
        if len(p) < 3:
            continue
        log = open(t[:-5] + ".log").read()
        m = re.search(r"Total Communication: (\d+) bytes", log)
        if "Verification succeeded" not in log:
            problems.append(f"{t}: otFPSI verification line missing")
        out.append(dict(wall=float(p[0]), cpu=float(p[1]) + float(p[2]), mb=int(m[1]) / 2**20 if m else None))
    return out


def med(rs, k):
    v = [r[k] for r in rs if r.get(k) is not None]
    return statistics.median(v) if v else None


def f(x):
    if x is None:
        return "--"
    if x < 10:
        return f"{x:.2f}"
    if x < 100:
        return f"{x:.1f}"
    return f"{x:,.0f}".replace(",", "{,}")


def write(name, rows):
    open(f"tables/{name}.tex", "w").write("\n".join(rows) + "\n")
    print(f"--- tables/{name}.tex"); print("\n".join(rows))


# A. versus otFPSI, d=128
rows = []
for delta in (4, 8, 16):
    for lg in (8, 10, 12, 14):
        o, u = otf(f"ot_d128_t{delta}_2e{lg}"), ours(f"ours_d128_t{delta}_2e{lg}")
        if not u:
            continue
        ow, oc, om = med(o, "wall"), med(o, "cpu"), med(o, "mb")
        uw, uc, um = med(u, "wall"), med(u, "cpu"), med(u, "mb")
        sp = f"{ow / uw:.1f}$\\times$" if ow else "--"
        cs = f"{oc / uc:.1f}$\\times$" if oc else "--"
        bq = f"{u[0]['B']}/--/{u[0]['Q']}$^\\dagger$" if u[0]["fb"] == "1" else f"{u[0]['B']}/{u[0]['U']}/{u[0]['Q']}"
        rows.append(f"{delta} & $2^{{{lg}}}$ & {f(ow)} & {f(oc)} & {f(om)} & {f(uw)} & {f(uc)} & {f(um)} & "
                    f"{bq} & {sp} & {cs} \\\\")
    rows.append("\\midrule")
write("c2_vsot", rows[:-1])

# A'. versus Gao et al. (pinned single runs, optional second argument = gao dir)
if len(sys.argv) > 2:
    rows = []
    for lg in (8, 10, 12):
        g = os.path.join(sys.argv[2], f"gao_m2e{lg}_n2e{lg}.log")
        if not os.path.exists(g):
            continue
        t = open(g).read()
        off = re.search(r"Offline done\s+([\d.]+)", t)
        on = re.search(r"online done\s+[\d.]+\s+([\d.]+)", t)
        cm = re.search(r"comm total:\s+([\d.]+)MB", t)
        u, o = ours(f"ours_d128_t4_2e{lg}"), otf(f"ot_d128_t4_2e{lg}")
        if not (off and on and cm and u):
            continue
        rows.append(f"$2^{{{lg}}}$ & {f(float(on[1]) / 1e3)} & {f(float(off[1]) / 1e3)} & {f(float(cm[1]))} & "
                    f"{f(med(o, 'wall'))} & {f(med(o, 'mb'))} & \\textbf{{{f(med(u, 'wall'))}}} & \\textbf{{{f(med(u, 'mb'))}}} \\\\")
    write("c2_cmp", rows)
    # network estimate at 2^12, delta=4: time + bits/bandwidth + rounds*RTT. Ours: 60 rounds (upper bound),
    # Gao: online time only and 0 rounds, otFPSI: wall time and 0 rounds (both favour the baselines).
    g = open(os.path.join(sys.argv[2], "gao_m2e12_n2e12.log")).read()
    gon = float(re.search(r"online done\s+[\d.]+\s+([\d.]+)", g)[1]) / 1e3
    gmb = float(re.search(r"comm total:\s+([\d.]+)MB", g)[1])
    u, o = ours("ours_d128_t4_2e12"), otf("ot_d128_t4_2e12")
    rows = []
    for name, bw, rtt in [("LAN (1\\,Gbps, 0.2\\,ms)", 1e9, 0.2e-3), ("WAN (100\\,Mbps, 80\\,ms)", 1e8, 80e-3)]:
        est = lambda t, mib, rounds: t + mib * 2**20 * 8 / bw + rounds * rtt
        rows.append(f"{name} & {f(est(gon, gmb, 0))} & {f(est(med(o, 'wall'), med(o, 'mb'), 0))} & "
                    f"\\textbf{{{f(est(med(u, 'wall'), med(u, 'mb'), 60))}}} \\\\")
    write("c2_net", rows)

# B. ablations at 2^12 (baseline = ours_d128_t4_2e12)
base = ours("ours_d128_t4_2e12")
rows = []
for tag, label in [("ours_d128_t4_2e12", "Full protocol, block cover $(5,1)$"),
                   ("abl_nocompress", "No payload compression"),
                   ("abl_uniqc", "UniqC mapping ($B=1$, drops colliding keys)"),
                   ("abl_pad44", "Padded, $\\bar B=\\bar U=4$"),
                   ("abl_b3r1", "Block cover $(3,1)$, $r=1$"),
                   ("abl_cover2", "CoveringLSH cover, $q=2$")]:
    u = ours(tag) if tag != "ours_d128_t4_2e12" else base
    if not u:
        continue
    rows.append(f"{label} & {f(med(u, 'wall'))} & {f(med(u, 'cpu'))} & {f(med(u, 'mb'))} & "
                f"{u[0]['B']}/{u[0]['U']}/{u[0]['Q']} & {u[0]['matches']} \\\\")
write("c2_abl", rows)

# C. scaling
rows = []
for tag, label in [("scale_d128_t4", "$128,4,(5,1)$")] + [("scale_d64_t3_b4s1", "$64,3,(4,1)$"), ("scale_d64_t3_b6s3", "$64,3,(6,3)$")]:
    for lg in ((16, 18, 20) if tag.startswith("scale_d128") else (14, 16, 18)):
        u = ours(f"{tag}_2e{lg}")
        if not u:
            continue
        x = u[0]
        if x["fb"] != "0":
            problems.append(f"{tag}_2e{lg}: fell back")
        rows.append(f"{label} & $2^{{{lg}}}$ & {f(x['wall'])} & {f(x['cpu'])} & {f(x['mb'])} & {x['B']}/{x['U']}/{x['Q']} & {1e3 * x['wall'] / 2**lg:.2f} \\\\")
write("c2_scale", rows)

# D. real data
rows = []
for tag, label in [("real_code", "CPython, ours"), ("real_code_uniqc", "CPython, UniqC mapping"),
                   ("real_code_pad64", "CPython, padded $\\bar B=\\bar U=64$"),
                   ("real_cifar", "CIFAR-10, ours"), ("real_cifar_uniqc", "CIFAR-10, UniqC mapping")]:
    u = ours(tag)
    if not u:
        continue
    rows.append(f"{label} & {f(med(u, 'wall'))} & {f(med(u, 'cpu'))} & {f(med(u, 'mb'))} & {u[0]['B']}/{u[0]['U']}/{u[0]['Q']} & {u[0]['matches']} \\\\")
for tag, label in [("ot_code", "CPython, otFPSI"), ("ot_cifar", "CIFAR-10, otFPSI")]:
    o = otf(tag)
    if o:
        rows.append(f"{label} & {f(med(o, 'wall'))} & {f(med(o, 'cpu'))} & {f(med(o, 'mb'))} & -- & -- \\\\")
write("c2_real", rows)

# E. block vs CoveringLSH covers (uniform, 2^12)
rows = []
for label, blk, cov, bsr, q in [("Uniform, $d=128$, $\\delta=4$", "ours_d128_t4_2e12", "abl_cover2", "(5,1,0)", 2),
                                ("Uniform, $d=128$, $\\delta=16$", "ours_d128_t16_2e12", "cov16_cover3", "(9,1,1)", 3)]:
    b, c = ours(blk), ours(cov)
    if not (b and c):
        continue
    rows.append(f"{label} & {bsr} & {b[0]['B']}/{b[0]['Q']} & {f(med(b, 'wall'))} & {q} & {c[0]['B']}/{c[0]['Q']} & {f(med(c, 'wall'))} \\\\")
write("c2_cover", rows)

# F. cost breakdown at 2^14 from fpsi_bench -v (sender timer; median over runs of each phase)
phases = [("S map", "Probing map"), ("S cpsi", "Payload sharing ($\\Fps$)"), ("S threshold", "Threshold circuit ($\\Fbool(\\Cthr)$)"),
          ("S permute", "Regrouping ($\\Fosn$)"), ("S or", "Aggregation ($\\Fbool(\\Cor)$)"), ("S deliver", "Delivery ($\\Fot$)")]
runs = []
for lf in sorted(glob.glob(os.path.join(D, "break_2e14_r*.log"))):
    t = open(lf).read()
    if "false_neg=0 false_pos=0" not in t:
        problems.append(f"{lf}: not verified exact")
    sender = t.split("sender")[1].split("receiver")[0] if "sender" in t else ""
    d = {}
    for key, _ in phases:
        m = re.search(re.escape(key) + r"\s+[\d.]+\s+([\d.]+)", sender)
        if m:
            d[key] = float(m[1]) / 1e3
    if len(d) == len(phases):
        runs.append(d)
if runs:
    meds = {k: statistics.median(r[k] for r in runs) for k, _ in phases}
    tot = sum(meds.values())
    rows = [f"{lab} & {meds[k]:.2f} & {100 * meds[k] / tot:.1f}\\% \\\\" for k, lab in phases]
    rows.append(f"\\midrule\nTotal & {tot:.2f} & \\\\")
    write("c2_break", rows)

print("\nPROBLEMS:" if problems else "\nall verified runs: FN=FP=0")
for p in problems:
    print(" ", p)
sys.exit(1 if problems else 0)
