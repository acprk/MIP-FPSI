"""Print cgroup nr_throttled events per minute of wall time for one run
(integer; 999999 if the run did not produce a summary)."""
import json
import sys

try:
    s = json.load(open(sys.argv[1]))
    d = s["nr_throttled_post"] - s["nr_throttled_pre"]
    print(int(round(d / (s["wall_s"] / 60.0))))
except Exception:
    print(999999)
