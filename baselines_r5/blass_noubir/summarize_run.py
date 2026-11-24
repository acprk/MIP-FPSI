#!/usr/bin/env python3
"""Parse one run directory (written by run_one.sh) into summary.json and
append a line to results.csv."""
import json
import os
import re
import sys

out, t0, t1, rrc, src, lns, lnr, ell, t = sys.argv[1:10]
wall = float(t1) - float(t0)


def parse_time(path):
    d = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if ":" not in line:
                continue
            k, v = line.rsplit(":", 1) if line.startswith("Elapsed") else line.split(":", 1)
            d[k.strip()] = v.strip()
    def num(key):
        try:
            return float(d[key])
        except (KeyError, ValueError):
            return None
    return {
        "user_s": num("User time (seconds)"),
        "sys_s": num("System time (seconds)"),
        "max_rss_kb": num("Maximum resident set size (kbytes)"),
        "exit_status": d.get("Exit status"),
        "vol_ctx": num("Voluntary context switches"),
        "invol_ctx": num("Involuntary context switches"),
        "pct_cpu": d.get("Percent of CPU this job got"),
    }


def grab(path, pattern):
    with open(path) as f:
        txt = f.read()
    m = re.search(pattern, txt)
    return m.groups() if m else None


def throttled(path):
    with open(path) as f:
        txt = f.read()
    m = re.search(r"nr_throttled (\d+)", txt)
    la = re.search(r"loadavg: (\S+ \S+ \S+)", txt)
    return (int(m.group(1)) if m else None, la.group(1) if la else None)


snd = parse_time(os.path.join(out, "time_sender.txt"))
rcv = parse_time(os.path.join(out, "time_receiver.txt"))
r = grab(os.path.join(out, "receiver.txt"),
         r"RECEIVER: ([\d.]+) MB total, ([\d.]+) s total \[OT: (\d+) ms, network: (\d+) ms, decryption: (\d+) ms\], KDerVolume: ([\d.]+)MByte")
s = grab(os.path.join(out, "sender.txt"),
         r"SENDER: (\d+) ms \[encrypt: (\d+) ms, kder: (\d+) ms, OT: (\d+) ms, network: (\d+) ms\]")
chk = grab(os.path.join(out, "receiver.txt"), r"CHECK: successful decryptions = (\d+) of (\d+)")
pre_thr, pre_la = throttled(os.path.join(out, "pre.txt"))
post_thr, post_la = throttled(os.path.join(out, "post.txt"))

cpu_total = None
if None not in (snd["user_s"], snd["sys_s"], rcv["user_s"], rcv["sys_s"]):
    cpu_total = snd["user_s"] + snd["sys_s"] + rcv["user_s"] + rcv["sys_s"]

summary = {
    "tag": os.path.basename(out),
    "logN_S": int(lns), "logN_R": int(lnr), "ELL": int(ell), "t": int(t),
    "exit_receiver": int(rrc), "exit_sender": int(src),
    "wall_s": round(wall, 3),
    "cpu_total_s": cpu_total,
    "sender": snd, "receiver": rcv,
    "receiver_report": None if r is None else {
        "comm_MB": float(r[0]), "recv_total_s": float(r[1]), "OT_ms": int(r[2]),
        "network_ms": int(r[3]), "decrypt_ms": int(r[4]), "KDerVolume_MB": float(r[5])},
    "sender_report": None if s is None else {
        "total_ms": int(s[0]), "encrypt_ms": int(s[1]), "kder_ms": int(s[2]),
        "OT_ms": int(s[3]), "network_ms": int(s[4])},
    "check_successes": None if chk is None else [int(chk[0]), int(chk[1])],
    "nr_throttled_pre": pre_thr, "nr_throttled_post": post_thr,
    "loadavg_pre": pre_la, "loadavg_post": post_la,
}
with open(os.path.join(out, "summary.json"), "w") as f:
    json.dump(summary, f, indent=1)

csv = os.path.join(os.path.dirname(out), "..", "results.csv")
header = ("tag,logN,ELL,t,wall_s,cpu_total_s,cpu_sender_s,cpu_receiver_s,"
          "sender_total_ms,sender_encrypt_ms,sender_kder_ms,recv_total_s,recv_decrypt_ms,"
          "comm_MB,maxrss_recv_kb,maxrss_send_kb,check,nr_throttled_delta,loadavg_pre,loadavg_post\n")
new = not os.path.exists(csv)
with open(csv, "a") as f:
    if new:
        f.write(header)
    rr = summary["receiver_report"] or {}
    sr = summary["sender_report"] or {}
    def c(x):
        return "" if x is None else str(x)
    f.write(",".join([
        summary["tag"], lns, ell, t, c(summary["wall_s"]), c(cpu_total and round(cpu_total, 2)),
        c(snd["user_s"] is not None and round(snd["user_s"] + snd["sys_s"], 2)),
        c(rcv["user_s"] is not None and round(rcv["user_s"] + rcv["sys_s"], 2)),
        c(sr.get("total_ms")), c(sr.get("encrypt_ms")), c(sr.get("kder_ms")),
        c(rr.get("recv_total_s")), c(rr.get("decrypt_ms")), c(rr.get("comm_MB")),
        c(rcv["max_rss_kb"]), c(snd["max_rss_kb"]),
        "" if chk is None else f"{chk[0]}/{chk[1]}",
        c(None if pre_thr is None or post_thr is None else post_thr - pre_thr),
        c(pre_la), c(post_la)]) + "\n")
print(json.dumps(summary, indent=1))
