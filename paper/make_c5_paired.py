"""Campaign 5 (scripts/run_campaign5_paired.sh): paired, randomized rounds.
usage: python3 make_c5_paired.py <campaign5_dir>
Per round k, paired ratios against otFPSI (SoftSpoken) with two denominators:
  proc  = process CPU of otFPSI (/usr/bin/time user+sys; includes input generation and verification)
  proto = protocol-scoped CPU of otFPSI (getrusage around its two interleaved protocol sections)
Ours: cpu_s of both parties (net_bench timer: protocol only, excludes input generation and the
plaintext check). Also paired build A/B and common-backend control ratios, and CIFAR repetitions.
Requires all 5 rounds; exits 1 on any missing, incomplete, unverified or inexact run."""
import os
import re
import statistics as st
import sys

D = sys.argv[1]
R = [1, 2, 3, 4, 5]
problems = []


def kv(t):
    return dict(re.findall(r"(\w+)=([\w.]+)", t))


def ours(tag):
    rf, sf = os.path.join(D, tag + ".recv.log"), os.path.join(D, tag + ".send.log")
    if not (os.path.exists(rf) and os.path.exists(sf)):
        problems.append(f"{tag}: missing")
        return None
    a, b = kv(open(rf).read()), kv(open(sf).read())
    if "cpu_s" not in a or "cpu_s" not in b:
        problems.append(f"{tag}: incomplete")
        return None
    if a.get("false_neg") != "0" or a.get("false_pos") != "0":
        problems.append(f"{tag}: FN/FP {a.get('false_neg')}/{a.get('false_pos')}")
    return dict(cpu=float(a["cpu_s"]) + float(b["cpu_s"]), wall=float(a["wall_s"]),
                mb=float(a["sent_MB"]) + float(a["recv_MB"]), B=a.get("B"), U=a.get("U"), Q=a.get("Q"),
                fb=a.get("fallback"))


def otf(tag):
    lf, tf = os.path.join(D, tag + ".log"), os.path.join(D, tag + ".time")
    if not (os.path.exists(lf) and os.path.exists(tf)):
        problems.append(f"{tag}: missing")
        return None
    log = open(lf).read()
    if log.count("Verification succeeded") < 2:
        problems.append(f"{tag}: not verified in both stages")
    pc = [float(x) for x in re.findall(r"PROTOCOL_CPU \w+ ([\d.]+)", log)]
    if len(pc) != 2:
        problems.append(f"{tag}: protocol CPU missing")
        return None
    e, u, s = map(float, open(tf).read().split()[:3])
    return dict(proc=u + s, proto=sum(pc), wall=e)


def fmt(v):
    return f"{st.median(v):.1f}x [{min(v):.1f}, {max(v):.1f}]"


ot12 = {k: otf(f"ot12_r{k}") for k in R}
otc = {k: otf(f"otcode_r{k}") for k in R}
for name, ot in (("2^12 d=128", ot12), ("CPython", otc)):
    v = [x for x in ot.values() if x]
    if v:
        print(f"otFPSI {name}: proc CPU {st.median(x['proc'] for x in v):.1f}, protocol CPU "
              f"{st.median(x['proto'] for x in v):.1f}, wall {st.median(x['wall'] for x in v):.1f} s")

jobs = [("code_exact", otc), ("code_pow2", otc), ("code_noU", otc), ("t16_exact", ot12), ("t16_pow2", ot12),
        ("t4_pow2_A", ot12), ("t4_pow2_B", ot12), ("t4_noU_A", ot12), ("t4_noU_B", ot12),
        ("t8_pow2_A", ot12), ("t8_pow2_B", ot12), ("t4_ours8", ot12), ("t4_ctrl", ot12),
        ("t8_ours8", ot12), ("t8_ctrl", ot12)]
runs = {}
for j, ot in jobs:
    rows = {k: ours(f"{j}_r{k}") for k in R}
    runs[j] = rows
    ok = [k for k in R if rows[k] and ot[k]]
    if not ok:
        continue
    cpu = [rows[k]["cpu"] for k in ok]
    proto = [ot[k]["proto"] / rows[k]["cpu"] for k in ok]
    proc = [ot[k]["proc"] / rows[k]["cpu"] for k in ok]
    wall = [ot[k]["wall"] / rows[k]["wall"] for k in ok]
    loads = sorted({f"{rows[k]['B']}/{rows[k]['U']}/{rows[k]['Q']} fb={rows[k]['fb']}" for k in ok})
    print(f"{j:11s} n={len(ok)} CPU {st.median(cpu):7.2f}  comm {st.median(rows[k]['mb'] for k in ok):7.1f} MiB  "
          f"vs proto {fmt(proto)}  vs proc {fmt(proc)}  wall {fmt(wall)}  {loads}")


def paired(a, b, label):
    v = [runs[a][k]["cpu"] / runs[b][k]["cpu"] for k in R if runs[a][k] and runs[b][k]]
    if v:
        print(f"{label}: CPU ratio {st.median(v):.3f} [{min(v):.3f}, {max(v):.3f}] over {len(v)} rounds")


paired("t4_pow2_A", "t4_pow2_B", "build A / build B, delta=4 rounded")
paired("t4_noU_A", "t4_noU_B", "build A / build B, delta=4 U-free")
paired("t8_pow2_A", "t8_pow2_B", "build A / build B, delta=8 rounded")
paired("t4_ctrl", "t4_ours8", "control (full payload + key check) / ours, delta=4")
paired("t8_ctrl", "t8_ours8", "control (full payload + key check) / ours, delta=8")
for md in ("exact", "pow2", "noU"):
    rows = [ours(f"cifar_{md}_r{k}") for k in (1, 2, 3)]
    rows = [r for r in rows if r]
    if rows:
        c = [r["cpu"] for r in rows]
        print(f"CIFAR {md:5s}: n={len(rows)} CPU median {st.median(c):.1f} [{min(c):.1f}, {max(c):.1f}], "
              f"wall {st.median(r['wall'] for r in rows):.1f}, comm {st.median(r['mb'] for r in rows):.1f} MiB")
print("PROBLEMS:" if problems else "all checks passed")
for p in problems:
    print(" ", p)
sys.exit(1 if problems else 0)
