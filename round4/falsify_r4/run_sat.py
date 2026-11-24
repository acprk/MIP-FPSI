"""Driver: for each (e,delta) run pw_sat with K=2^(e-d) (positive control, expect SAT)
and K=2^(e-d)+1 (kill test K2 for C3'; K1 for C3 would be 2^(e-d+1)+1). 4 workers max."""
import subprocess, sys, os
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
emax = int(sys.argv[1]) if len(sys.argv) > 1 else 6
tmo = int(sys.argv[2]) if len(sys.argv) > 2 else 900
jobs = []
for e in range(2, emax + 1):
    for d in range(1, e):
        base = 1 << (e - d)
        jobs.append((e, d, base))
        jobs.append((e, d, base + 1))

def run(j):
    e, d, K = j
    r = subprocess.run([sys.executable, os.path.join(HERE, "pw_sat.py"), str(e), str(d), str(K), str(tmo)],
                       capture_output=True, text=True)
    return (r.stdout + r.stderr).strip()

with ThreadPoolExecutor(4) as ex:
    for line in ex.map(run, jobs):
        print(line, flush=True)
