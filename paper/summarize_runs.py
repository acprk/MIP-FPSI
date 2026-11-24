"""Summarize a results directory of net_bench runs (tag.{send,recv}.log [+ .time]) and otfpsi runs.

Usage: python3 summarize_runs.py <results_dir>
For each tag (rep suffix _rK stripped): number of reps, median/min/max total CPU seconds of both
parties (from the logs' cpu_s), comm MiB, B/U/Q, peak RSS per party (if /usr/bin/time -v files
exist), and FN/FP status. otfpsi logs: process CPU (from .time) and protocol-scoped CPU
(PROTOCOL_CPU lines, if present). Exits 1 if any run failed verification or is incomplete.
"""
import re
import statistics as st
import sys
from collections import defaultdict
from pathlib import Path

d = Path(sys.argv[1])
groups = defaultdict(list)
problems = []
capped = (d / "summary.txt").read_text() if (d / "summary.txt").exists() else ""


def num(pat, text, cast=float):
    m = re.search(pat, text)
    return cast(m.group(1)) if m else None


for recv in sorted(d.glob("*.recv.log")):
    tag = recv.name[: -len(".recv.log")]
    send = d / f"{tag}.send.log"
    rt, stx = recv.read_text(errors="replace"), send.read_text(errors="replace") if send.exists() else ""
    cpu_r, cpu_s = num(r"cpu_s=([\d.]+)", rt), num(r"cpu_s=([\d.]+)", stx)
    row = {"tag": tag}
    if cpu_r is None or cpu_s is None:
        # Only runs that the campaign script itself stopped at its memory cap are declared;
        # any other incomplete run is a failure.
        if f"{tag} killed_mem=1" in capped:
            row["status"] = "stopped at memory cap"
        else:
            row["status"] = "INCOMPLETE"
            problems.append(tag)
    else:
        row["cpu"] = cpu_r + cpu_s
        row["wall"] = num(r"wall_s=([\d.]+)", rt)
        row["comm"] = (num(r"sent_MB=([\d.]+)", rt) or 0) + (num(r"recv_MB=([\d.]+)", rt) or 0)
        row["BUQ"] = "/".join(str(num(p, rt, int)) for p in (r" B=(\d+)", r" U=(\d+)", r" Q=(\d+)"))
        fn, fp = num(r"false_neg=(\d+)", rt, int), num(r"false_pos=(\d+)", rt, int)
        row["status"] = "exact" if fn == 0 and fp == 0 else f"FN={fn} FP={fp}"
        if row["status"] != "exact":
            problems.append(tag)
    rss = []
    for side in ("send", "recv"):
        tf = d / f"{tag}.{side}.time"
        if tf.exists():
            rss.append(num(r"Maximum resident set size \(kbytes\): (\d+)", tf.read_text(), int))
    if rss and None not in rss:
        row["rss_gb"] = max(rss) / 2**20
    groups[re.sub(r"_r\d+$", "", tag)].append(row)

for log in sorted(d.glob("*.log")):
    if log.name.endswith((".recv.log", ".send.log")):
        continue
    tag = log.stem
    text = log.read_text(errors="replace")
    tf = d / f"{tag}.time"
    row = {"tag": tag, "status": "verified" if text.count("Verification succeeded") >= 2 else "UNVERIFIED"}
    if tf.exists():
        e, u, s = map(float, tf.read_text().split()[:3])
        row["cpu"], row["wall"] = u + s, e
    pc = [float(x) for x in re.findall(r"PROTOCOL_CPU \w+ ([\d.]+)", text)]
    if len(pc) == 2:
        row["proto_cpu"] = sum(pc)
    if row["status"] != "verified":
        problems.append(tag)
    groups[re.sub(r"_r\d+$", "", tag)].append(row)


def med(rows, key):
    v = [r[key] for r in rows if key in r]
    return (st.median(v), min(v), max(v), len(v)) if v else None


for g, rows in sorted(groups.items()):
    parts = [f"{g:22s} reps={len(rows)}"]
    for key, fmt in (("cpu", "CPU {:.2f} [{:.2f},{:.2f}]"), ("proto_cpu", "protoCPU {:.2f} [{:.2f},{:.2f}]"),
                     ("wall", "wall {:.1f}"), ("comm", "comm {:.1f}MiB"), ("rss_gb", "RSS {:.2f}GiB")):
        m = med(rows, key)
        if m:
            parts.append(fmt.format(*m[: fmt.count("{")]))
    buq = sorted({r.get("BUQ") for r in rows if r.get("BUQ")})
    if buq:
        parts.append("B/U/Q " + ",".join(buq))
    parts.append(",".join(sorted({r["status"] for r in rows})))
    print("  ".join(parts))
if problems:
    print("PROBLEMS:", " ".join(problems))
    sys.exit(1)
