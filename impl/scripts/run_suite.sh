#!/usr/bin/env bash
# Final benchmark suite. Both parties run as coroutines in ONE process and ONE thread over an
# in-memory channel (so wall time = CPU time of both parties together), pinned to two dedicated
# physical cores. Each configuration is repeated; tables report the median.
# Usage: scripts/run_suite.sh [CORES] (default 38,39)
set -euo pipefail
cd "$(dirname "$0")/.."
CORES="${1:-38,39}"
BIN=./build/fpsi_bench
OUT=../results/final_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
{ echo "cores=$CORES host=$(hostname) start=$(date)"; cat /proc/loadavg; git -C ../third_party/volepsi log --oneline -1; } > "$OUT/env.txt"

run() { # reps name args...
  local reps=$1 name=$2; shift 2
  for k in $(seq 1 "$reps"); do
    echo "== $name rep$k: $* load=$(cut -d' ' -f1 /proc/loadavg)" >> "$OUT/summary.txt"
    taskset -c "$CORES" $BIN "$@" > "$OUT/${name}_rep$k.log" 2>&1 || echo "FAILED rc=$?" >> "$OUT/${name}_rep$k.log"
    grep -E "time_s|matches" "$OUT/${name}_rep$k.log" >> "$OUT/summary.txt"
  done
}

# 1. balanced uniform, d=128 delta=4 (Gao's benchmark setting)
for lg in 8 10 12 14; do n=$((1 << lg)); run 5 bal_d128_2e$lg -m $n -n $n -d 128 -delta 4 -b 5 -s 1 -indexverify; done
run 3 bal_d128_2e16 -m 65536 -n 65536 -d 128 -delta 4 -b 5 -s 1 -indexverify
# 1b. zero data-dependent leakage: pad to public bounds
run 3 pad_d128_2e12 -m 4096 -n 4096 -d 128 -delta 4 -b 5 -s 1 -Bbar 4 -Ubar 4 -indexverify
# 2. unbalanced
for lm in 8 10; do for ln in 12 16; do
  run 3 unb_m2e${lm}_n2e${ln}_b5 -m $((1<<lm)) -n $((1<<ln)) -b 5 -s 1 -indexverify
  run 3 unb_m2e${lm}_n2e${ln}_b3 -m $((1<<lm)) -n $((1<<ln)) -b 3 -s 1 -indexverify
done; done
# 3. other (d, delta)
run 3 d64_delta3_b6s3_2e12 -m 4096 -n 4096 -d 64 -delta 3 -b 6 -s 3 -indexverify
run 3 d64_delta3_b4s1_2e12 -m 4096 -n 4096 -d 64 -delta 3 -b 4 -s 1 -indexverify
run 3 d256_delta8_b9s1_2e12 -m 4096 -n 4096 -d 256 -delta 8 -b 9 -s 1 -indexverify
# 4. real near-duplicate data (parameters chosen by a rule on the receiver's data only)
run 3 code_simhash64 -data ../data/code_simhash64.bin -d 64 -delta 3 -b 4 -s 1
run 3 cifar_mild -data ../data/cifar_dhash64_mild.bin -d 64 -delta 4 -b 7 -s 3 -indexverify
# 5. generic all-pairs baseline
run 3 allpairs_d128_2e8 -m 256 -n 256 -allpairs -indexverify
run 3 allpairs_d128_2e10 -m 1024 -n 1024 -allpairs -indexverify
run 1 allpairs_d128_2e12 -m 4096 -n 4096 -allpairs -indexverify
run 1 allpairs_code -data ../data/code_simhash64.bin -d 64 -delta 3 -allpairs
# 6. instrumented breakdown
run 1 breakdown_2e14 -m 16384 -n 16384 -v -indexverify
echo "done=$(date)" >> "$OUT/env.txt"
