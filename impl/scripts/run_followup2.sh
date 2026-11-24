#!/usr/bin/env bash
# Follow-up to run_campaign2.sh (same binary build3, same cores 38/39), started after it finishes:
#  * instrumented cost breakdown at 2^14 (fpsi_bench -v, one process, sender-side timer)
#  * block vs CoveringLSH covers at delta=16 (net_bench, two processes)
# Usage: scripts/run_followup2.sh <campaign_dir>
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=$1
until grep -q "^done=" "$OUT/env.txt"; do sleep 60; done
if ps -C otfpsi,net_bench,fpsi_bench -o pid= | read -r _; then
  echo "another benchmark is running; refusing to start" >&2; exit 1
fi
NB=./build3/net_bench
FB=./build3/fpsi_bench
PORT=1600
echo "followup start=$(date) load=$(cut -d' ' -f1 /proc/loadavg)" >> "$OUT/env.txt"
for k in 1 2 3; do
  taskset -c 38 $FB -m 16384 -n 16384 -d 128 -delta 4 -b 5 -s 1 -v > "$OUT/break_2e14_r$k.log" 2>&1
done
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
  ours "cov16_cover3_r$k" -m 4096 -n 4096 -d 128 -delta 16 -cover 3
done
echo "followup done=$(date)" >> "$OUT/env.txt"
