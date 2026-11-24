"""Tables from campaign3 (scripts/run_campaign3.sh).
usage: python3 make_c3_tables.py <campaign3_dir>  -> tables/c3_*.tex
Primary metric: total CPU seconds of both parties (ours: sender + receiver getrusage; otFPSI:
user + sys of its two-thread process). Medians over repetitions, with min-max spread. otFPSI is
compared in its faster OT mode per size (silent or softspoken). Every run of ours must be exact."""
import glob
import os
import re
import statistics
import sys

D = sys.argv[1]
os.makedirs("tables", exist_ok=True)
problems = []
MODES = ["exact", "pow2", "noU", "dp"]
SKIPPED = re.compile(r"^d128_t16_2e12_(noU|dp)_r\d+\.recv\.log$")


def kv(t):
    return dict(re.findall(r"(\w+)=([\w.]+)", t))


def ours(pat):
    out = []
    for rf in sorted(glob.glob(os.path.join(D, pat + ".recv.log"))):
        sf = rf.replace(".recv.log", ".send.log")
        r, s = open(rf).read(), (open(sf).read() if os.path.exists(sf) else "")
        if "wall_s" not in r or "wall_s" not in s:
            # Only the configurations recorded in skipped.txt (delta=16, 2^12, U-free/DP) may be incomplete.
            if not SKIPPED.search(os.path.basename(rf)):
                problems.append(f"{rf}: incomplete run")
            continue
        a, b = kv(r), kv(s)
        if a.get("false_neg") != "0" or a.get("false_pos") != "0":
            problems.append(f"{rf}: FN/FP {a.get('false_neg')}/{a.get('false_pos')}")
        out.append(dict(cpu=float(a["cpu_s"]) + float(b["cpu_s"]), wall=float(a["wall_s"]),
                        mb=float(a["sent_MB"]) + float(a["recv_MB"]), B=a.get("B"), U=a.get("U"), Q=a.get("Q"),
                        fb=a.get("fallback", "0")))
    return out


def otf(pat):
    out = []
    for t in sorted(glob.glob(os.path.join(D, pat + ".time"))):
        p = open(t).read().split()
        if len(p) < 3 or not p[0].replace(".", "").isdigit():
            continue
        log = open(t[:-5] + ".log").read()
        m = re.search(r"Total Communication: (\d+) bytes", log)
        out.append(dict(wall=float(p[0]), cpu=float(p[1]) + float(p[2]), mb=int(m[1]) / 2**20 if m else None))
    return out


def med(rs, k):
    v = [r[k] for r in rs if r.get(k) is not None]
    return statistics.median(v) if v else None


def spread(rs, k):
    v = [r[k] for r in rs if r.get(k) is not None]
    return (min(v), max(v)) if v else (None, None)


def f(x):
    if x is None:
        return "--"
    if x < 10:
        return f"{x:.2f}"
    if x < 100:
        return f"{x:.1f}"
    return f"{x:,.0f}".replace(",", "{,}")


def loads(u):
    """B/U/Q over all runs; a range a--b where the runs differ (DP noise)"""
    out = []
    for k in ("B", "U", "Q"):
        v = sorted({int(r[k]) for r in u if r.get(k) is not None})
        out.append(str(v[0]) if len(v) == 1 else f"{v[0]}--{v[-1]}")
    return "/".join(out)


PENDING = {}


def write(name, rows):
    # Tables are written only after every validation check has passed (see the end of the script).
    PENDING[name] = "\n".join(rows) + "\n"
    print(f"--- tables/{name}.tex (pending validation)")
    print("\n".join(rows))


def best_ot(prefix):
    """faster otFPSI OT mode by median CPU"""
    cands = []
    for om in ("silent", "soft"):
        r = otf(f"{prefix.format(om=om)}_r*")
        if r:
            cands.append((med(r, "cpu"), om, r))
    return min(cands) if cands else None


# A. versus otFPSI, d=128, CPU seconds
rows, ot_rows = [], []
for lg in (8, 10, 12):
    for om in ("silent", "soft"):
        r = otf(f"ot_{om}_2e{lg}_r*")
        if r:
            lo, hi = spread(r, "cpu")
            ot_rows.append(f"$2^{{{lg}}}$ & {om} & {f(med(r, 'cpu'))} & [{f(lo)}, {f(hi)}] & {f(med(r, 'wall'))} & {f(med(r, 'mb'))} & {len(r)} \\\\")
write("c3_otfpsi", ot_rows)
for delta in (4, 8, 16):
    for lg in (8, 10, 12):
        bo = best_ot(f"ot_{{om}}_2e{lg}")
        cells = []
        for md in MODES:
            u = ours(f"d128_t{delta}_2e{lg}_{md}_r*")
            if not u:
                cells.append("-- & --")
                continue
            c = med(u, "cpu")
            sp = f"{bo[0] / c:.1f}$\\times$" if bo else "--"
            mark = "$^\\dagger$" if u[0]["fb"] == "1" else ""
            cells.append(f"{f(c)}{mark} & {sp}")
        u0 = ours(f"d128_t{delta}_2e{lg}_exact_r*")
        bq = f"{u0[0]['B']}/{u0[0]['Q']}" if u0 else "--"
        rows.append(f"{delta} & $2^{{{lg}}}$ & {f(bo[0]) if bo else '--'} & {bq} & " + " & ".join(cells) + " \\\\")
    rows.append("\\midrule")
write("c3_vsot", rows[:-1])

# B. communication per mode (MiB), d=128, 2^12
rows = []
for delta in (4, 8, 16):
    cells = [f(med(ours(f"d128_t{delta}_2e12_{md}_r*"), "mb")) for md in MODES]
    bo = best_ot("ot_{om}_2e12")
    rows.append(f"{delta} & {f(med(bo[2], 'mb')) if bo else '--'} & " + " & ".join(cells) + " \\\\")
write("c3_comm", rows)

# C. real data
rows = []
for name, pre, otpre in [("CPython", "real_code", "ot_code_{om}"), ("CIFAR-10", "real_cifar", None)]:
    for md in MODES:
        u = ours(f"{pre}_{md}_r*")
        if u:
            label = {"exact": "exact", "pow2": "rounded", "noU": "U-free", "dp": "DP"}[md]
            rows.append(f"{name} & {label} & {f(med(u, 'cpu'))} & {f(med(u, 'wall'))} & {f(med(u, 'mb'))} & {loads(u)} \\\\")
    if otpre:
        # report BOTH OT modes: SoftSpoken is faster, silent OT sends far less, and the
        # comparison must not silently pick whichever mode flatters us on each metric.
        for om in ("soft", "silent"):
            r = otf(f"{otpre.format(om=om)}_r*")
            if r:
                rows.append(f"{name} & otFPSI ({om}) & {f(med(r, 'cpu'))} & {f(med(r, 'wall'))} & {f(med(r, 'mb'))} & -- \\\\")
    else:
        # CIFAR: SoftSpoken run of this campaign if present, plus the silent-OT run of campaign2
        so = otf("ot_soft_cifar_r*")
        if so:
            rows.append(f"{name} & otFPSI (soft) & {f(med(so, 'cpu'))} & {f(med(so, 'wall'))} & {f(med(so, 'mb'))} & -- \\\\")
        c2 = os.path.join(os.path.dirname(D.rstrip('/')), "campaign2_20261023_1344", "ot_cifar")
        if os.path.exists(c2 + ".time"):
            p = open(c2 + ".time").read().split()
            mm = re.search(r"Total Communication: (\d+) bytes", open(c2 + ".log").read())
            rows.append(f"{name} & otFPSI (silent) & {f(float(p[1]) + float(p[2]))} & {f(float(p[0]))} & {f(int(mm[1]) / 2**20)} & -- \\\\")
write("c3_real", rows)

# D. block vs CoveringLSH covers at 2^12 (CPU seconds, exact mode)
rows = []
for label, blk, cov, bsr, q in [("Uniform, $d=128$, $\\delta=4$", "d128_t4_2e12_exact_r*", "cov4_cover2_r*", "(5,1,0)", 2),
                                ("Uniform, $d=128$, $\\delta=16$", "d128_t16_2e12_exact_r*", "cov16_cover3_r*", "(9,1,1)", 3)]:
    b, c = ours(blk), ours(cov)
    if b and c:
        rows.append(f"{label} & {bsr} & {b[0]['B']}/{b[0]['Q']} & {f(med(b, 'cpu'))} & {q} & {c[0]['B']}/{c[0]['Q']} & {f(med(c, 'cpu'))} \\\\")
write("c3_cover", rows)

# E. cost breakdown at 2^14 (fpsi_bench -v, one process; sender-side phase timers, median per phase)
phases = [("S map", "Probing map"), ("S cpsi", "Payload sharing ($\\Fps$)"), ("S threshold", "Threshold circuit ($\\Fbool(\\Cthr)$)"),
          ("S permute", "Regrouping ($\\Fosn$)"), ("S or", "Aggregation ($\\Fbool(\\Cor)$)"), ("S deliver", "Delivery ($\\Fot$)")]
runs = []
for lf in sorted(glob.glob(os.path.join(D, "break_2e14_r*.log"))):
    t = open(lf).read()
    if "false_neg=0 false_pos=0" not in t:
        problems.append(f"{lf}: not verified exact")
        continue
    sender = t.split("sender")[1].split("receiver")[0] if "sender" in t else ""
    dd = {}
    for key, _ in phases:
        mm = re.search(re.escape(key) + r"\s+[\d.]+\s+([\d.]+)", sender)
        if mm:
            dd[key] = float(mm[1]) / 1e3
    cpu = re.search(r"cpu_s\(both parties\)=([\d.]+)", t)
    if len(dd) == len(phases) and cpu:
        dd["cpu"] = float(cpu[1])
        runs.append(dd)
if runs:
    meds = {k: statistics.median(r[k] for r in runs) for k, _ in phases}
    tot = sum(meds.values())
    rows = [f"{lab} & {100 * meds[k] / tot:.1f}\\% \\\\" for k, lab in phases]
    rows.append(f"\\midrule\nTotal CPU (both parties) & {statistics.median(r['cpu'] for r in runs):.1f}\\,s \\\\")
    write("c3_break", rows)

# F. validation: expected repetition counts (skipped configurations are listed in skipped.txt)
skipped = open(os.path.join(D, "skipped.txt")).read() if os.path.exists(D + "/skipped.txt") else ""
for delta, reps in ((4, 5), (8, 5), (16, 3)):
    for lg in (8, 10, 12):
        for md in MODES:
            got = len(ours(f"d128_t{delta}_2e{lg}_{md}_r*"))
            heavy = delta == 16 and lg == 12 and md in ("noU", "dp")
            if got != reps and not (heavy and "delta 16" in skipped):
                problems.append(f"d128_t{delta}_2e{lg}_{md}: {got} of {reps} runs")
for lg in (8, 10, 12):
    for om in ("silent", "soft"):
        r = otf(f"ot_{om}_2e{lg}_r*")
        if len(r) != 5:
            problems.append(f"otFPSI {om} 2^{lg}: {len(r)} of 5 runs")
        for t in sorted(glob.glob(os.path.join(D, f"ot_{om}_2e{lg}_r*.log"))):
            if open(t).read().count("Verification succeeded") < 2:  # distance and threshold stages
                problems.append(f"{t}: otFPSI not verified in both stages")
# real data: CPython 3 runs per mode, CIFAR 1 run per mode, as planned in run_campaign3.sh
for pre, reps in (("real_code", 3), ("real_cifar", 1)):
    for md in MODES:
        got = len(ours(f"{pre}_{md}_r*"))
        if got != reps:
            problems.append(f"{pre}_{md}: {got} of {reps} runs")

print("\nPROBLEMS:" if problems else "\nall checks passed: runs complete, ours FN=FP=0, otFPSI verified")
for p in problems:
    print(" ", p)
if problems:
    print("no table written")
    sys.exit(1)
for name, text in PENDING.items():
    open(f"tables/{name}.tex", "w").write(text)
print(f"wrote {len(PENDING)} tables")
