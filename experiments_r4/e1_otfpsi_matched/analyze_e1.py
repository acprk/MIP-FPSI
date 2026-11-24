"""Analysis of E1 (run_e1.sh): output-matched otFPSI vs native otFPSI vs our protocol, paired by round.
CPU = protocol-scoped CPU of both parties (otFPSI: getrusage around its protocol sections; or_deliver
and net_bench: their own timers). Exits 1 on any missing, failed or inexact run.
usage: python3 analyze_e1.py <results_dir>"""
import os
import re
import statistics as st
import sys

D = sys.argv[1]
R = [1, 2, 3, 4, 5]
problems = []


def txt(p):
    p = os.path.join(D, p)
    if not os.path.exists(p):
        problems.append(f"missing {p}")
        return ""
    return open(p).read()


def proto_cpu(log):
    v = [float(x) for x in re.findall(r"PROTOCOL_CPU \w+ ([\d.]+)", log)]
    return sum(v) if len(v) == 2 else None


def comm_mib(log):
    m = re.search(r"Total Communication: (\d+) bytes", log)
    return int(m[1]) / 2**20 if m else None


def elapsed(timefile):
    m = re.search(r"Elapsed \(wall clock\) time \(h:mm:ss or m:ss\): ([\d:.]+)", timefile)
    if not m:
        return None
    parts = [float(x) for x in m[1].split(":")]
    return sum(p * 60 ** (len(parts) - 1 - i) for i, p in enumerate(parts))


def rss_gib(timefile):
    m = re.search(r"Maximum resident set size \(kbytes\): (\d+)", timefile)
    return int(m[1]) / 2**20 if m else None


def kv(t):
    return dict(re.findall(r"(\w+)=([\w.]+)", t))


def matched(tag):
    s1 = txt(f"{tag}.s1.log")
    if "MATCHED_CHECK" not in s1 or "mismatches=0" not in s1:
        problems.append(f"{tag}: stage-1 check failed")
    r, s = kv(txt(f"{tag}.s2r.log")), kv(txt(f"{tag}.s2s.log"))
    if r.get("false_neg") != "0" or r.get("false_pos") != "0":
        problems.append(f"{tag}: output not exact")
    pc = proto_cpu(s1)
    s2cpu = float(r["cpu_s"]) + float(s["cpu_s"])
    return dict(cpu=pc + s2cpu, s1cpu=pc, s2cpu=s2cpu,
                wall=elapsed(txt(f"{tag}.s1.time")) + float(r["wall_s"]),
                comm=comm_mib(s1) + float(r["sent_MB"]) + float(r["recv_MB"]),
                rss=rss_gib(txt(f"{tag}.s1.time")))


def native(tag):
    log = txt(f"{tag}.log")
    if log.count("Verification succeeded") < 2:
        problems.append(f"{tag}: native otFPSI not verified")
    e, u, s = map(float, txt(f"{tag}.time").split()[:3])
    return dict(cpu=proto_cpu(log), proc=u + s, wall=e, comm=comm_mib(log))


def ours(tag):
    r, s = kv(txt(f"{tag}.recv.log")), kv(txt(f"{tag}.send.log"))
    if r.get("false_neg") != "0" or r.get("false_pos") != "0":
        problems.append(f"{tag}: not exact")
    return dict(cpu=float(r["cpu_s"]) + float(s["cpu_s"]), wall=float(r["wall_s"]),
                comm=float(r["sent_MB"]) + float(r["recv_MB"]))


def fmt(v):
    return f"{st.median(v):.2f} [{min(v):.2f}, {max(v):.2f}]"


for size, mt, nt, pre in (("2^12, d=128, delta=4", "m12", "n12", "o12"), ("CPython, d=64, delta=3", "mcode", "ncode", "oc")):
    M = {k: matched(f"{mt}_r{k}") for k in R}
    N = {k: native(f"{nt}_r{k}") for k in R}
    print(f"== {size}")
    print(f"native otFPSI   CPU {fmt([N[k]['cpu'] for k in R])}  wall {st.median(N[k]['wall'] for k in R):.1f}  comm {st.median(N[k]['comm'] for k in R):.0f} MiB")
    print(f"matched otFPSI  CPU {fmt([M[k]['cpu'] for k in R])}  (stage1 {st.median(M[k]['s1cpu'] for k in R):.1f}, OR+delivery {st.median(M[k]['s2cpu'] for k in R):.2f})"
          f"  wall {st.median(M[k]['wall'] for k in R):.1f}  comm {st.median(M[k]['comm'] for k in R):.0f} MiB  RSS {st.median(M[k]['rss'] for k in R):.2f} GiB")
    print(f"  matched/native CPU ratio {fmt([M[k]['cpu'] / N[k]['cpu'] for k in R])}")
    for md in ("exact", "pow2", "noU"):
        O = {k: ours(f"{pre}_{md}_r{k}") for k in R}
        print(f"ours {md:5s}      CPU {fmt([O[k]['cpu'] for k in R])}  wall {st.median(O[k]['wall'] for k in R):.1f}  comm {st.median(O[k]['comm'] for k in R):.1f} MiB"
              f"  | vs matched: CPU {fmt([M[k]['cpu'] / O[k]['cpu'] for k in R])}x  wall {fmt([M[k]['wall'] / O[k]['wall'] for k in R])}x"
              f"  | vs native: CPU {fmt([N[k]['cpu'] / O[k]['cpu'] for k in R])}x")
print("PROBLEMS:" if problems else "all checks passed")
for p in problems:
    print(" ", p)
sys.exit(1 if problems else 0)
