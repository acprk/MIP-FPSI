#!/usr/bin/env bash
# Leakage-optimal mode (-pow2: B, U rounded up to powers of two) on the main configurations.
# build4 = build3 + the pow2Leak option (default off, otherwise identical code path).
# Usage: scripts/run_followup3.sh <campaign_dir>   (waits for run_followup2.sh to finish)
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=$1
until grep -q "^followup done=" "$OUT/env.txt"; do sleep 60; done
if ps -C otfpsi,net_bench,fpsi_bench -o pid= | read -r _; then
  echo "another benchmark is running; refusing to start" >&2; exit 1
fi
NB=./build4/net_bench
PORT=1700
echo "followup3 start=$(date) load=$(cut -d' ' -f1 /proc/loadavg)" >> "$OUT/env.txt"
ours() {
  local tag=$1; shift
  PORT=$((PORT + 1))
  taskset -c 39 $NB -role 1 -addr 127.0.0.1:$PORT -indexverify "$@" > "$OUT/$tag.recv.log" 2>&1 &
  local rp=$!
  sleep 0.3
  taskset -c 38 $NB -role 0 -addr 127.0.0.1:$PORT "$@" > "$OUT/$tag.send.log" 2>&1
  wait $rp
  echo "$tag load=$(cut -d' ' -f1 /proc/loadavg) $(grep -h -E 'wall_s|matches' "$OUT/$tag.recv.log" "$OUT/$tag.send.log" | tr '\n' ' ')" >> "$OUT/summary.txt"
}
for k in 1 2 3; do
  ours "pow2_d128_t4_2e12_r$k" -m 4096 -n 4096 -d 128 -delta 4 -b 5 -s 1 -pow2
  ours "pow2_d128_t8_2e12_r$k" -m 4096 -n 4096 -d 128 -delta 8 -b 9 -s 1 -pow2
  ours "pow2_real_code_r$k" -data ../data/code_simhash64.bin -d 64 -delta 3 -b 4 -s 1 -pow2
done
ours "pow2_d128_t16_2e12_r1" -m 4096 -n 4096 -d 128 -delta 16 -b 9 -s 1 -pow2
ours "pow2_d128_t4_2e14_r1" -m 16384 -n 16384 -d 128 -delta 4 -b 5 -s 1 -pow2
ours "pow2_real_cifar" -data ../data/cifar_dhash64_mild.bin -d 64 -delta 4 -b 7 -s 3 -pow2
echo "followup3 done=$(date)" >> "$OUT/env.txt"
