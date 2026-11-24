#!/usr/bin/env bash
# otFPSI with its natural thread count: each party spawns one helper thread per phase (4 threads),
# so give it 4 physical cores (36-39; HT siblings 88-91 are not used by us). Runs after campaign3.
# Usage: scripts/run_c3_ot4.sh <campaign3_dir>
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=$1
until grep -q "^done=" "$OUT/env.txt"; do sleep 120; done
OT=../baselines/others/xdup/01_otfpsi/build/otfpsi
CG=/sys/fs/cgroup/user.slice/user-1000.slice/cpu.stat
thr() { grep nr_throttled "$CG" | cut -d' ' -f2; }
gate() { while [ "$(cut -d' ' -f1 /proc/loadavg | cut -d. -f1)" -ge 30 ]; do sleep 60; done; }
echo "ot4 start=$(date)" >> "$OUT/env.txt"
for k in 1 2 3; do
  for lg in 10 12; do n=$((1 << lg))
    for om in silent softspoken; do
      gate; t0=$(thr); tag="ot4_${om}_2e${lg}_r$k"
      /usr/bin/time -f "%e %U %S" -o "$OUT/$tag.time" taskset -c 36-39 $OT -proto_distance batched -proto_threshold np \
        -querier_size $n -responder_size $n -vector_size 128 -threshold 4 -prf aes -ot $om > "$OUT/$tag.log" 2>&1
      echo "$tag load=$(cut -d' ' -f1 /proc/loadavg) thr=$(( $(thr) - t0 )) $(cat "$OUT/$tag.time")" >> "$OUT/summary.txt"
    done
  done
done
echo "ot4 done=$(date)" >> "$OUT/env.txt"
