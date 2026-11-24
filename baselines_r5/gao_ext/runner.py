#!/usr/bin/env python3
"""Sequential, pinned timing runner (baselines_r5).

Usage: runner.py JOBS.jsonl OUTDIR
Each job line: {"tag": str, "kind": "gao"|"ours"|"generic", "cmd": [argv...],
                "cwd": optional, "env": optional dict, "cores": optional "42,43"}
Per run it records: leftover benchmark processes (ps -C ...), /proc/loadavg and the user
cgroup nr_throttled before/after, /usr/bin/time -v of the pinned command, stdout/stderr.
For kind=gao it also samples the process CPU (utime+stime, all threads) at the moment the
artifact prints "offline done", so online CPU = total CPU - CPU at that mark.
Jobs run strictly one at a time. Results are appended to OUTDIR/summary.jsonl.
"""
import json, os, re, subprocess, sys, time

CG = "/sys/fs/cgroup/user.slice/user-1000.slice/cpu.stat"
PS_NAMES = "main,net_bench,fpsi_bench,otfpsi,simulation,run,flash-psi"
CORES = "42,43"
TICK = os.sysconf("SC_CLK_TCK")


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
    """PIDs of listed benchmark binaries (not ours) whose affinity is restricted and hits 42/43."""
    hits = []
    out = sh(["ps", "-C", PS_NAMES, "-o", "pid="])
    for tok in out.split():
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
        if cpus & {42, 43, 94, 95}:
            hits.append(pid)
    return hits


def child_pid(pid):
    try:
        with open(f"/proc/{pid}/task/{pid}/children") as f:
            kids = f.read().split()
        return int(kids[0]) if kids else None
    except OSError:
        return None


def proc_cpu(pid):
    with open(f"/proc/{pid}/stat") as f:
        fields = f.read().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / TICK  # utime, stime (whole process)


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


def parse_gao(text):
    d = {}
    m = re.search(r"Offline done\s+([\d.]+)", text)
    if m:
        d["offline_wall_s"] = float(m.group(1)) / 1e3
    m = re.search(r"online done\s+([\d.]+)\s+([\d.]+)", text)
    if m:
        d["total_wall_s"] = float(m.group(1)) / 1e3
        d["online_wall_s"] = float(m.group(2)) / 1e3
    m = re.search(r"comm total:\s+([\d.]+)MB", text)
    if m:
        d["comm_MiB"] = float(m.group(1))
    m = re.search(r"Intersection Set Size:(\d+)", text)
    if m:
        d["intersection"] = int(m.group(1))
    m = re.search(r"side_length\s*:\s*(\d+)", text)
    if m:
        d["P_bits"] = int(m.group(1))
    return d


def parse_ours(text):
    d = {}
    for k, pat in [("proto_wall_s", r"time_s=([\d.eE+-]+)"), ("proto_cpu_s", r"cpu_s\(both parties\)=([\d.eE+-]+)"),
                   ("comm_MiB", r"comm_MB=([\d.eE+-]+)"), ("matches", r"matches=(\d+)"), ("truth", r"truth=(\d+)"),
                   ("false_neg", r"false_neg=(\d+)"), ("false_pos", r"false_pos=(\d+)"), ("B", r" B=(\d+)"),
                   ("U", r" U=(\d+)"), ("Q", r" Q=(\d+)"), ("fallback", r"fallback=(\d+)")]:
        m = re.search(pat, text)
        if m:
            d[k] = float(m.group(1)) if "." in m.group(1) or "e" in m.group(1) else int(m.group(1))
    return d


def gate(own):
    waited = 0
    while True:
        la = float(loadavg().split()[0])
        foreign = foreign_on_cores(own)
        if la < 30 and not foreign:
            return waited
        time.sleep(60)
        waited += 60


def run_job(job, outdir):
    tag, kind, cmd = job["tag"], job.get("kind", "generic"), job["cmd"]
    cores = job.get("cores", CORES)
    pre = os.path.join(outdir, tag)
    rec = {"tag": tag, "kind": kind, "cmd": cmd, "cores": cores}
    rec["gate_wait_s"] = gate({os.getpid()})
    rec["ps_before"] = sh(["ps", "-C", PS_NAMES, "-o", "pid,psr,etime,args"]).strip()
    rec["load_before"] = loadavg()
    rec["thr_before"] = throttled()
    rec["start"] = time.strftime("%Y-%m-%d %H:%M:%S %Z")
    env = dict(os.environ)
    env.update(job.get("env", {}))
    full = ["/usr/bin/time", "-v", "-o", pre + ".time", "taskset", "-c", cores] + cmd
    with open(pre + ".log", "w") as lo:
        p = subprocess.Popen(full, stdout=lo, stderr=subprocess.STDOUT, cwd=job.get("cwd"), env=env)
        mark_cpu = None
        cpid = None
        while p.poll() is None:
            time.sleep(0.25)
            if kind == "gao" and mark_cpu is None:
                cpid = cpid or child_pid(p.pid)
                try:
                    with open(pre + ".log") as f:
                        if "offline done" in f.read():
                            mark_cpu = proc_cpu(cpid)
                except (OSError, TypeError):
                    pass
    rec["end"] = time.strftime("%Y-%m-%d %H:%M:%S %Z")
    rec["load_after"] = loadavg()
    rec["thr_after"] = throttled()
    rec["thr_delta"] = rec["thr_after"] - rec["thr_before"]
    rec["rc"] = p.returncode
    rec.update(parse_time_v(pre + ".time"))
    with open(pre + ".log") as f:
        text = f.read()
    if kind == "gao":
        rec.update(parse_gao(text))
        rec["cpu_total_s"] = rec.get("user_s", 0) + rec.get("sys_s", 0)
        if mark_cpu is not None:
            rec["cpu_at_offline_mark_s"] = mark_cpu
            rec["online_cpu_s"] = rec["cpu_total_s"] - mark_cpu
    elif kind == "ours":
        rec.update(parse_ours(text))
        rec["cpu_total_s"] = rec.get("user_s", 0) + rec.get("sys_s", 0)
    with open(os.path.join(outdir, "summary.jsonl"), "a") as f:
        f.write(json.dumps(rec) + "\n")
    return rec


def main():
    jobs_path, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    done = set()
    sp = os.path.join(outdir, "summary.jsonl")
    if os.path.exists(sp):
        with open(sp) as f:
            done = {json.loads(l)["tag"] for l in f if l.strip()}
    with open(jobs_path) as f:
        jobs = [json.loads(l) for l in f if l.strip()]
    for job in jobs:
        if job["tag"] in done:
            continue
        # optional condition: skip if a referenced run's total wall exceeds a limit
        cond = job.get("skip_if_total_over")
        if cond:
            ref, limit, factor = cond
            with open(sp) as f:
                refs = [json.loads(l) for l in f if l.strip()]
            r = [x for x in refs if x["tag"] == ref]
            if r and r[0].get("total_wall_s", 0) * factor > limit:
                with open(sp, "a") as f2:
                    f2.write(json.dumps({"tag": job["tag"], "skipped": True,
                                         "reason": f"projected {r[0]['total_wall_s']*factor:.0f}s > {limit}s from {ref}"}) + "\n")
                continue
        rec = run_job(job, outdir)
        print(f"{rec['end']} {rec['tag']} rc={rec['rc']} wall={rec.get('wall_s')} cpu={rec.get('cpu_total_s')}", flush=True)


if __name__ == "__main__":
    main()
