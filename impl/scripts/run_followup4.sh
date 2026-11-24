#!/usr/bin/env bash
# Follow-ups that need a quiet host (replaces run_followup2/3, which were throttled on 10/23 19:00
# by other jobs sharing the user's 32-core cgroup quota).
#  * cost breakdown at 2^14 (build3 fpsi_bench -v)
#  * block vs CoveringLSH cover at delta=16 (build3)
#  * leakage-optimal mode (-pow2) paired with exact-B runs of the same binary (build4), interleaved
# Before every run: wait until load1 < 10; log load and cgroup nr_throttled delta per run.
# Usage: scripts/run_followup4.sh <campaign_dir>
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=$1
CG=/sys/fs/cgroup/user.slice/user-1000.slice/cpu.stat
if ps -C otfpsi,net_bench,fpsi_bench -o pid= | read -r _; then
  echo "another benchmark is running; refusing to start" >&2; exit 1
fi
quiet() { while [ "$(cut -d' ' -f1 /proc/loadavg | cut -d. -f1)" -ge 10 ]; do sleep 60; done; }
thr() { grep nr_throttled "$CG" | cut -d' ' -f2; }
PORT=1800
echo "followup4 start=$(date)" >> "$OUT/env.txt"
ours() { # bin tag args...
  local nb=$1 tag=$2; shift 2
  quiet
  local t0; t0=$(thr)
  PORT=$((PORT + 1))
  taskset -c 39 $nb -role 1 -addr 127.0.0.1:$PORT -indexverify "$@" > "$OUT/$tag.recv.log" 2>&1 &
  local rp=$!
  sleep 0.3
  taskset -c 38 $nb -role 0 -addr 127.0.0.1:$PORT "$@" > "$OUT/$tag.send.log" 2>&1
  wait $rp
  echo "$tag load=$(cut -d' ' -f1 /proc/loadavg) throttled_delta=$(( $(thr) - t0 )) $(grep -h -E 'wall_s|matches' "$OUT/$tag.recv.log" "$OUT/$tag.send.log" | tr '\n' ' ')" >> "$OUT/summary.txt"
}
for k in 1 2 3; do
  quiet
  t0=$(thr)
  taskset -c 38 ./build3/fpsi_bench -m 16384 -n 16384 -d 128 -delta 4 -b 5 -s 1 -v > "$OUT/break_2e14_r$k.log" 2>&1
  echo "break_2e14_r$k load=$(cut -d' ' -f1 /proc/loadavg) throttled_delta=$(( $(thr) - t0 ))" >> "$OUT/summary.txt"
done
for k in 1 2 3; do
  ours ./build3/net_bench "cov16_cover3_r$k" -m 4096 -n 4096 -d 128 -delta 16 -cover 3
done
B4=./build4/net_bench
for k in 1 2 3; do
  for pw in exact pow2; do
    f=""; [ $pw = pow2 ] && f="-pow2"
    ours $B4 "${pw}_d128_t8_2e12_r$k" -m 4096 -n 4096 -d 128 -delta 8 -b 9 -s 1 $f
    ours $B4 "${pw}_real_code_r$k" -data ../data/code_simhash64.bin -d 64 -delta 3 -b 4 -s 1 $f
    ours $B4 "${pw}_d128_t4_2e14_r$k" -m 16384 -n 16384 -d 128 -delta 4 -b 5 -s 1 $f
  done
done
for pw in exact pow2; do
  f=""; [ $pw = pow2 ] && f="-pow2"
  ours $B4 "${pw}_d128_t16_2e12_r1" -m 4096 -n 4096 -d 128 -delta 16 -b 9 -s 1 $f
  ours $B4 "${pw}_real_cifar" -data ../data/cifar_dhash64_mild.bin -d 64 -delta 4 -b 7 -s 3 $f
done
echo "followup4 done=$(date)" >> "$OUT/env.txt"
