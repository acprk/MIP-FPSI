"""Collect every E3 run in results/ into results.csv and print a summary.
CPU = user+sys of the whole process from /usr/bin/time -v (both parties: one process, party threads;
for matched runs stage 1 + stage 2). proto_cpu = getrusage around the protocol sections only.
usage: python3 analyze_e3.py"""
import csv
import glob
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.join(HERE, "results")


def rd(p):
    return open(p).read() if os.path.exists(p) else ""


def tv(t):
    """user, sys, wall, max RSS (GiB) from /usr/bin/time -v"""
    if not t:
        return None
    u = float(re.search(r"User time \(seconds\): ([\d.]+)", t)[1])
    s = float(re.search(r"System time \(seconds\): ([\d.]+)", t)[1])
    w = re.search(r"Elapsed \(wall clock\) time \(h:mm:ss or m:ss\): ([\d:.]+)", t)[1]
    parts = [float(x) for x in w.split(":")]
    wall = sum(p * 60 ** (len(parts) - 1 - i) for i, p in enumerate(parts))
    rss = int(re.search(r"Maximum resident set size \(kbytes\): (\d+)", t)[1]) / 2**20
    return u, s, wall, rss


def env(p):
    t = rd(p)
    b = re.search(r"^before .*loadavg=([\d.]+)\S* nr_throttled=(\d+)", t, re.M)
    a = re.search(r"^after .*loadavg=([\d.]+)\S* nr_throttled=(\d+)", t, re.M)
    ex = re.findall(r"exit=(\d+)", t)
    return dict(load_before=b and b[1], load_after=a and a[1],
                throttled=(int(a[2]) - int(b[2])) if a and b else None,
                exits=",".join(ex), date=(re.search(r"before date=(\S+)", t) or [None, None])[1])


def proto(log):
    v = dict(re.findall(r"PROTOCOL_CPU (\w+) ([\d.]+)", log))
    return sum(float(x) for k, x in v.items())


def comm(log):
    m = re.search(r"Total Communication: (\d+) bytes", log)
    return int(m[1]) / 2**20 if m else None


rows = []
for e in sorted(glob.glob(os.path.join(R, "*.env"))):
    tag = os.path.basename(e)[:-4]
    E = env(e)
    row = dict(tag=tag, **E)
    if os.path.exists(os.path.join(R, tag + ".s1.log")):  # matched
        s1, s2 = rd(os.path.join(R, tag + ".s1.log")), rd(os.path.join(R, tag + ".s2.log"))
        t1, t2 = tv(rd(os.path.join(R, tag + ".s1.time"))), tv(rd(os.path.join(R, tag + ".s2.time")))
        if not (t1 and t2):
            continue
        kv = dict(re.findall(r"(\w+)=([\w.]+)", s2))
        ok = ("MATCHED_CHECK checked=all mismatches=0" in s1 and kv.get("false_neg") == "0"
              and kv.get("false_pos") == "0")
        row.update(kind="matched", cpu=t1[0] + t1[1] + t2[0] + t2[1], user=t1[0] + t2[0], sys=t1[1] + t2[1],
                   wall=t1[2] + t2[2], rss_gib=max(t1[3], t2[3]), proto_cpu=proto(s1) + float(kv["cpu_s"]),
                   s1_cpu=t1[0] + t1[1], s2_cpu=t2[0] + t2[1],
                   comm_mib=comm(s1) + float(kv["sent_MB"]) + float(kv["recv_MB"]), verified=ok,
                   check=(re.search(r"MATCHED_CHECK full.*", s1) or [""])[0] + " | " + s2.strip().splitlines()[-1])
    else:
        log = rd(os.path.join(R, tag + ".log"))
        t = tv(rd(os.path.join(R, tag + ".time")))
        if not t:
            continue
        if "REAL_NATIVE" in log:
            ok = "REAL_VERIFY OK" in log
            chk = (re.search(r"REAL_CHECK.*", log) or [""])[0]
            kind = "native_realpath"
        else:
            ok = log.count("Verification succeeded") == 2
            chk = "artifact self-check (distances + threshold on independent random data)"
            kind = "artifact_random"
        row.update(kind=kind, cpu=t[0] + t[1], user=t[0], sys=t[1], wall=t[2], rss_gib=t[3],
                   proto_cpu=proto(log), comm_mib=comm(log), verified=ok, check=chk)
    rows.append(row)

cols = ["tag", "kind", "cpu", "user", "sys", "proto_cpu", "s1_cpu", "s2_cpu", "wall", "rss_gib", "comm_mib",
        "verified", "load_before", "load_after", "throttled", "exits", "date", "check"]
with open(os.path.join(HERE, "results.csv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
    w.writeheader()
    for r in rows:
        w.writerow({k: (round(v, 2) if isinstance(v, float) else v) for k, v in r.items()})
for r in rows:
    print(f"{r['tag']:28s} {r['kind']:16s} cpu={r['cpu']:9.1f} (u {r['user']:.1f} s {r['sys']:.1f}) proto={r['proto_cpu']:9.1f}"
          f" wall={r['wall']:8.1f} rss={r['rss_gib']:.2f}GiB comm={r['comm_mib']:.0f}MiB ok={r['verified']}"
          f" load={r['load_before']}->{r['load_after']} thr+={r['throttled']}")
