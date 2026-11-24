"""Paired, randomized head-to-head (scripts/run_campaign4_paired.sh).
usage: python3 make_c4_paired.py <campaign4_dir>
For every repetition k: ratio = CPU(otFPSI SoftSpoken, rep k) / CPU(ours, mode, rep k), both parties.
Prints medians and min-max of the paired ratios, CPU medians, and checks that every run of ours is
exact and every otFPSI run verified. Exits non-zero on any problem."""
import glob
import os
import re
import statistics
import sys

D = sys.argv[1]
problems = []


def kv(t):
    return dict(re.findall(r"(\w+)=([\w.]+)", t))


def ours(tag):
    r, s = open(os.path.join(D, tag + ".recv.log")).read(), open(os.path.join(D, tag + ".send.log")).read()
    a, b = kv(r), kv(s)
    if a.get("false_neg") != "0" or a.get("false_pos") != "0":
        problems.append(f"{tag}: FN/FP {a.get('false_neg')}/{a.get('false_pos')}")
    return float(a["cpu_s"]) + float(b["cpu_s"]), float(a["sent_MB"]) + float(a["recv_MB"]), a.get("B"), a.get("U")


def otf(tag):
    p = open(os.path.join(D, tag + ".time")).read().split()
    log = open(os.path.join(D, tag + ".log")).read()
    if log.count("Verification succeeded") < 2:  # distance and threshold stages
        problems.append(f"{tag}: otFPSI not verified in both stages")
    tt = re.search(r"Total Time: ([\d.]+) ms", log)
    return float(p[1]) + float(p[2]), float(p[0]), float(tt[1]) / 1e3 if tt else None


reps = [1, 2, 3, 4, 5]  # the campaign plan: five rounds
for k in reps:
    if not os.path.exists(os.path.join(D, f"ot_soft_r{k}.time")):
        print(f"otFPSI round {k} missing")
        sys.exit(1)
ot = {k: otf(f"ot_soft_r{k}") for k in reps}
print(f"otFPSI SoftSpoken 2^12: reps={len(reps)} CPU median {statistics.median(v[0] for v in ot.values()):.1f} s, "
      f"wall {statistics.median(v[1] for v in ot.values()):.1f} s, protocol-scoped wall {statistics.median(v[2] for v in ot.values()):.1f} s")
for d in (4, 8):
    for md in ("exact", "pow2", "noU"):
        cpu, ratios, mbs, loads = [], [], [], set()
        for k in reps:
            tag = f"d{d}_{md}_r{k}"
            if not os.path.exists(os.path.join(D, tag + ".recv.log")):
                problems.append(f"{tag}: missing")
                continue
            c, mb, B, U = ours(tag)
            cpu.append(c)
            mbs.append(mb)
            loads.add(f"{B}/{U}")
            ratios.append(ot[k][0] / c)
        if ratios:
            print(f"delta={d} {md:5s}: CPU median {statistics.median(cpu):.2f} s, comm {statistics.median(mbs):.1f} MiB, "
                  f"B/U {sorted(loads)}, paired speed-up median {statistics.median(ratios):.1f}x "
                  f"[{min(ratios):.1f}, {max(ratios):.1f}] over {len(ratios)} reps")
print("PROBLEMS:" if problems else "all checks passed")
for p in problems:
    print(" ", p)
sys.exit(1 if problems else 0)
