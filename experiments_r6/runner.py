#!/usr/bin/env python3
"""Sequential, pinned timing runner for experiments_r6 (adapted from baselines_r5/gao_ext/runner.py).

Usage: runner.py JOBS.jsonl OUTDIR
Job line: {"tag": str, "kind": "ours"|"otfpsi", "cmd": [argv...], plus free metadata fields}
Per attempt it records: ps -C of benchmark binaries, /proc/loadavg and the user-cgroup
nr_throttled before/after, /usr/bin/time -v (user+sys CPU, max RSS, wall) of the pinned command,
and the tool's own protocol-scoped CPU/wall/comm. All attempts go to OUTDIR/summary.jsonl.
Throttle rule: an attempt with more than 100 throttled periods per minute of wall time is marked
excluded and re-run (at most MAX_ATT attempts per tag; if all are throttled, the least throttled
attempt is kept and flagged). Jobs run strictly one at a time, on cores 40,41 only.
"""
import json, os, re, subprocess, sys, time

CG = "/sys/fs/cgroup/user.slice/user-1000.slice/cpu.stat"
PS_NAMES = "fpsi_bench,fpsi_bench_kc,otfpsi,otfpsi_inproc,net_bench,main"
CORES = "40,41"
GUARD = {40, 41, 92, 93}  # the two cores and their HT siblings
MAX_ATT = 3
THR_PER_MIN = 100


def sh(args):
    return subprocess.run(args, capture_output=True, text=True).stdout


def loadavg():
    with open("/proc/loadavg") as f:
        return f.read().strip()


def throttled():
    with open(CG) as f:
        for line in f:
            k, v = line.split()
            if k == "nr_throttled":
                return int(v)
    return -1


def foreign_on_cores(own_pids):
    hits = []
    for tok in sh(["ps", "-C", PS_NAMES, "-o", "pid="]).split():
        pid = int(tok)
        if pid in own_pids:
            continue
        try:
            with open(f"/proc/{pid}/status") as f:
                st = f.read()
        except OSError:
            continue
        m = re.search(r"Cpus_allowed_list:\s*(\S+)", st)
        if not m or m.group(1) == "0-103":
            continue
        cpus = set()
        for part in m.group(1).split(","):
            a, _, b = part.partition("-")
            cpus.update(range(int(a), int(b or a) + 1))
        if cpus & GUARD:
            hits.append(pid)
    return hits


def gate():
    waited = 0
    while True:
        la = float(loadavg().split()[0])
        if la < 30 and not foreign_on_cores({os.getpid()}):
            return waited
        time.sleep(30)
        waited += 30


def parse_time_v(path):
    d = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("User time"):
                d["user_s"] = float(line.split(":")[-1])
            elif line.startswith("System time"):
                d["sys_s"] = float(line.split(":")[-1])
            elif line.startswith("Maximum resident"):
                d["maxrss_kb"] = int(line.split(":")[-1])
            elif line.startswith("Elapsed (wall"):
                v = line.split("):")[-1].strip().split(":")
                d["wall_s"] = sum(float(x) * 60 ** i for i, x in enumerate(reversed(v)))
            elif line.startswith("Exit status"):
                d["exit"] = int(line.split(":")[-1])
    return d


def num(s):
    return float(s) if any(c in s for c in ".eE") else int(s)


def parse_ours(text):
    d = {}
    for k, pat in [("proto_wall_s", r"time_s=([\d.eE+-]+)"), ("proto_cpu_s", r"cpu_s\(both parties\)=([\d.eE+-]+)"),
                   ("comm_MiB", r"comm_MB=([\d.eE+-]+)"), ("matches", r"matches=(\d+)"), ("truth", r"truth=(\d+)"),
                   ("false_neg", r"false_neg=(\d+)"), ("false_pos", r"false_pos=(\d+)"), ("B", r" B=(\d+)"),
                   ("U", r" U=(\d+)"), ("Q", r" Q=(\d+)"), ("T", r" T=(\d+)"), ("bins", r" bins=(\d+)"),
                   ("tau", r" tau=(\d+)"), ("fallback", r"fallback=(\d+)")]:
        m = re.search(pat, text)
        if m:
            d[k] = num(m.group(1))
    return d


def parse_ot(text):
    d = {}
    m = re.search(r"PROTOCOL_CPU distance ([\d.]+)", text)
    if m:
        d["cpu_distance_s"] = float(m.group(1))
    m = re.search(r"PROTOCOL_CPU threshold ([\d.]+)", text)
    if m:
        d["cpu_threshold_s"] = float(m.group(1))
    if "cpu_distance_s" in d and "cpu_threshold_s" in d:
        d["proto_cpu_s"] = d["cpu_distance_s"] + d["cpu_threshold_s"]
    m = re.search(r"Total Time: ([\d.]+) ms", text)
    if m:
        d["proto_wall_s"] = float(m.group(1)) / 1e3
    m = re.search(r"Total Communication: (\d+) bytes", text)
    if m:
        d["comm_MiB"] = int(m.group(1)) / 2**20
    d["verif_ok"] = text.count("Verification succeeded!")
    d["verif_fail"] = text.count("Verification failed")
    return d


def run_attempt(job, outdir, att):
    tag = job["tag"]
    pre = os.path.join(outdir, f"{tag}.a{att}")
    rec = {k: v for k, v in job.items()}
    rec["attempt"] = att
    rec["cores"] = CORES
    rec["gate_wait_s"] = gate()
    rec["ps_before"] = sh(["ps", "-C", PS_NAMES, "-o", "pid,psr,etime,args"]).strip()
    rec["load_before"] = loadavg()
    rec["thr_before"] = throttled()
    rec["start"] = time.strftime("%Y-%m-%d %H:%M:%S %Z")
    full = ["/usr/bin/time", "-v", "-o", pre + ".time", "taskset", "-c", CORES] + job["cmd"]
    with open(pre + ".log", "w") as lo:
        rc = subprocess.run(full, stdout=lo, stderr=subprocess.STDOUT).returncode
    rec["end"] = time.strftime("%Y-%m-%d %H:%M:%S %Z")
    rec["load_after"] = loadavg()
    rec["thr_after"] = throttled()
    rec["thr_delta"] = rec["thr_after"] - rec["thr_before"]
    rec["rc"] = rc
    rec.update(parse_time_v(pre + ".time"))
    rec["cpu_total_s"] = rec.get("user_s", 0) + rec.get("sys_s", 0)
    with open(pre + ".log") as f:
        text = f.read()
    rec.update(parse_ours(text) if job["kind"] == "ours" else parse_ot(text))
    w = max(rec.get("wall_s", 0.0), 1.0)
    rec["thr_per_min"] = rec["thr_delta"] * 60.0 / w
    rec["throttled_excluded"] = rec["thr_per_min"] > THR_PER_MIN
    if job["kind"] == "ours":
        rec["correct"] = (rc == 0 and rec.get("false_neg") == 0 and rec.get("false_pos") == 0)
    else:
        rec["correct"] = (rc == 0 and rec.get("verif_ok", 0) == 2 and rec.get("verif_fail", 0) == 0)
    return rec


def main():
    jobs_path, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    sp = os.path.join(outdir, "summary.jsonl")
    done = set()
    if os.path.exists(sp):
        with open(sp) as f:
            for l in f:
                if l.strip():
                    r = json.loads(l)
                    if r.get("final"):
                        done.add(r["tag"])
    with open(jobs_path) as f:
        jobs = [json.loads(l) for l in f if l.strip()]
    for job in jobs:
        if job["tag"] in done:
            continue
        recs = []
        for att in range(1, MAX_ATT + 1):
            rec = run_attempt(job, outdir, att)
            recs.append(rec)
            ok = not rec["throttled_excluded"]
            if ok or att == MAX_ATT:
                break
            with open(sp, "a") as f:
                rec["final"] = False
                f.write(json.dumps(rec) + "\n")
            print(f"{rec['end']} {job['tag']} a{att} THROTTLED {rec['thr_per_min']:.0f}/min -> rerun", flush=True)
        last = recs[-1]
        if last["throttled_excluded"]:
            # all attempts throttled: keep the least throttled one as final, flag it
            best = dict(min(recs, key=lambda r: r["thr_per_min"]))
            last["final"] = False
            best["final"] = True
            best["all_attempts_throttled"] = True
            with open(sp, "a") as f:
                f.write(json.dumps(last) + "\n")
                f.write(json.dumps(best) + "\n")
            last = best
        else:
            last["final"] = True
            with open(sp, "a") as f:
                f.write(json.dumps(last) + "\n")
        print(f"{last['end']} {job['tag']} rc={last['rc']} wall={last.get('wall_s')} cpu={last.get('cpu_total_s'):.2f} "
              f"proto_cpu={last.get('proto_cpu_s')} thr/min={last['thr_per_min']:.0f} correct={last['correct']}", flush=True)


if __name__ == "__main__":
    main()
