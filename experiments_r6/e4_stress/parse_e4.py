"""Parse E4 logs into runs.csv (one row per run) and summary.csv (median per configuration)."""
import csv
import glob
import os
import re
import statistics
import sys

E = os.path.dirname(os.path.abspath(__file__))
rows = []
for path in sorted(glob.glob(os.path.join(E, "logs", "*.log"))):
    tag = os.path.basename(path)[:-4]
    txt = open(path).read()
    if "# end" not in txt:
        continue
    r = {"tag": tag}
    ma = re.match(r"a_k(\d+)_(\w+)_r(\d+)$", tag)
    mb = re.match(r"b_m(\d+)_n(\d+)_(\w+)_r(\d+)$", tag)
    if ma:
        r.update(part="a", k=int(ma.group(1)), m=4096, n=4096, mode=ma.group(2), rep=int(ma.group(3)))
    else:
        r.update(part="b", k="", m=int(mb.group(1)), n=int(mb.group(2)), mode=mb.group(3), rep=int(mb.group(4)))

    def g(pat, cast=float, default=""):
        mm = re.search(pat, txt)
        return cast(mm.group(1)) if mm else default

    r["exit"] = g(r"# end .* exit=(\d+)", int)
    for key in ("B", "U", "Q", "bins", "prog", "tau", "fallback"):
        r[key] = g(r"\b%s=(\d+)" % key, int)
    r["branch"] = {1: "all-pairs", 0: "index"}.get(r["fallback"], "")
    r["time_s"] = g(r"time_s=([\d.e+-]+)")
    r["cpu_s"] = g(r"cpu_s\(both parties\)=([\d.e+-]+)")
    r["comm_MB"] = g(r"comm_MB=([\d.e+-]+)")
    r["matches"] = g(r"matches=(\d+)", int)
    r["truth"] = g(r"truth=(\d+)", int)
    r["false_neg"] = g(r"false_neg=(\d+)", int)
    r["false_pos"] = g(r"false_pos=(\d+)", int)
    r["user_s"] = g(r"User time \(seconds\): ([\d.]+)")
    r["sys_s"] = g(r"System time \(seconds\): ([\d.]+)")
    r["maxrss_GiB"] = round(g(r"Maximum resident set size \(kbytes\): (\d+)", int, 0) / 2**20, 3)
    r["loadavg_before"] = g(r"loadavg_before=([\d.]+)")
    r["loadavg_after"] = g(r"loadavg_after=([\d.]+)")
    r["nr_throttled_delta"] = g(r"nr_throttled_delta=(-?\d+)", int)
    r["error"] = g(r"(abort: .*|std::bad_alloc|Killed|terminate.*)", str)
    rows.append(r)

fields = ["tag", "part", "k", "m", "n", "mode", "rep", "exit", "B", "U", "Q", "bins", "prog", "tau",
          "fallback", "branch", "time_s", "cpu_s", "user_s", "sys_s", "comm_MB", "maxrss_GiB", "matches",
          "truth", "false_neg", "false_pos", "loadavg_before", "loadavg_after", "nr_throttled_delta", "error"]
with open(os.path.join(E, "runs.csv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=fields)
    w.writeheader()
    for r in rows:
        w.writerow(r)

groups = {}
for r in rows:
    # pass 1 = r1-r3 (host loadavg ~27 during part b), pass 2 = r4-r6 (rerun_noise.sh, loadavg ~15)
    r["pass"] = 1 if r["rep"] <= 3 else 2
    groups.setdefault((r["part"], r["k"], r["m"], r["n"], r["mode"], r["pass"]), []).append(r)
order = {"exact": 0, "rounded": 1, "ufree": 2}
sfields = ["part", "pass", "k", "m", "n", "mode", "runs", "loadavg_med", "all_exact", "B", "U", "Q", "nTU_prog", "branch",
           "cpu_s_med", "cpu_s_min", "cpu_s_max", "wall_s_med", "comm_MB", "maxrss_GiB_max", "nr_throttled_delta_sum"]
out = []
for key in sorted(groups, key=lambda k: (k[0], k[5], k[1] if k[1] != "" else 0, k[2], k[3], order.get(k[4], 9))):
    rs = groups[key]
    ok = [r for r in rs if r["exit"] == 0 and r["false_neg"] == 0 and r["false_pos"] == 0]
    cpus = [r["cpu_s"] for r in rs if r["cpu_s"] != ""]
    walls = [r["time_s"] for r in rs if r["time_s"] != ""]
    out.append({
        "part": key[0], "pass": key[5], "k": key[1], "m": key[2], "n": key[3], "mode": key[4], "runs": len(rs),
        "loadavg_med": statistics.median([r["loadavg_before"] for r in rs]),
        "all_exact": "yes" if len(ok) == len(rs) else "NO (%d/%d)" % (len(ok), len(rs)),
        "B": rs[0]["B"], "U": rs[0]["U"], "Q": rs[0]["Q"], "nTU_prog": rs[0]["prog"], "branch": rs[0]["branch"],
        "cpu_s_med": round(statistics.median(cpus), 2) if cpus else "",
        "cpu_s_min": round(min(cpus), 2) if cpus else "", "cpu_s_max": round(max(cpus), 2) if cpus else "",
        "wall_s_med": round(statistics.median(walls), 2) if walls else "",
        "comm_MB": round(statistics.median([r["comm_MB"] for r in rs if r["comm_MB"] != ""]), 1) if cpus else "",
        "maxrss_GiB_max": max(r["maxrss_GiB"] for r in rs),
        "nr_throttled_delta_sum": sum(r["nr_throttled_delta"] or 0 for r in rs),
    })
with open(os.path.join(E, "summary.csv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=sfields)
    w.writeheader()
    for r in out:
        w.writerow(r)
for r in out:
    print(",".join(str(r[k]) for k in sfields))
bad = [r["tag"] for r in rows if not (r["exit"] == 0 and r["false_neg"] == 0 and r["false_pos"] == 0)]
print("runs=%d not_exact_or_failed=%s" % (len(rows), bad), file=sys.stderr)
