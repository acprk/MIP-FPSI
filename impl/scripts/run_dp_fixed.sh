#!/usr/bin/env bash
# Fixed-cover DP + U-free deployment configuration (round-D request).
# The cover is fixed by the public rule (b, s) = (delta + 1, 1), chosen before any data is seen.
# Release: B~ = B + max(0, s + Z), then rounded to a power of two; U-free (receiver learns m only).
# Per party: CPU (user+sys) and peak RSS from /usr/bin/time -v; runs are killed if the two parties
# together exceed 40 GB RSS. build7 = build6 + error-path fixes (no change on the measured path).
set -uo pipefail
cd "$(dirname "$0")/.."
if ps -C otfpsi,net_bench,fpsi_bench -o pid= | read -r _; then
  echo "another benchmark is running; refusing to start" >&2; exit 1
fi
NB=./build7/net_bench
OUT=../results/dp_fixed_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
{ echo "start=$(date)"; cat /proc/loadavg; sha256sum $NB; } > "$OUT/env.txt"
PORT=23700
LIMIT_KB=$((40 * 1024 * 1024))

ours() { # tag args...
  local tag=$1; shift
  PORT=$((PORT + 1))
  /usr/bin/time -v -o "$OUT/$tag.recv.time" taskset -c 47 $NB -role 1 -addr 127.0.0.1:$PORT -indexverify "$@" > "$OUT/$tag.recv.log" 2>&1 &
  local rp=$!
  sleep 0.3
  /usr/bin/time -v -o "$OUT/$tag.send.time" taskset -c 46 $NB -role 0 -addr 127.0.0.1:$PORT "$@" > "$OUT/$tag.send.log" 2>&1 &
  local sp=$!
  local killed=0
  while kill -0 $sp 2>/dev/null || kill -0 $rp 2>/dev/null; do
    local rss
    rss=$(ps -o rss= --ppid $sp,$rp 2>/dev/null | awk '{s+=$1} END {print s+0}')
    if [ "$rss" -gt "$LIMIT_KB" ]; then
      pkill -P $sp; pkill -P $rp; killed=1
    fi
    sleep 2
  done
  wait $sp; wait $rp
  echo "$tag killed_mem=$killed load=$(cut -d' ' -f1 /proc/loadavg) $(grep -h -E 'wall_s|matches|B=' "$OUT/$tag.recv.log" | tr '\n' ' ')" >> "$OUT/summary.txt"
}

DP="-noU -pow2 -nofallback"
for k in 1 2 3; do
  ours "code_eps1_r$k"   -data ../data/code_simhash64.bin -d 64 -delta 3 -b 4 -s 1 $DP -dpeps 1
  ours "syn12_eps1_r$k"  -m 4096 -n 4096 -d 128 -delta 4 -b 5 -s 1 $DP -dpeps 1
  ours "syn10_eps01_r$k" -m 1024 -n 1024 -d 128 -delta 4 -b 5 -s 1 $DP -dpeps 0.1
done
ours "code_eps01_r1"  -data ../data/code_simhash64.bin -d 64 -delta 3 -b 4 -s 1 $DP -dpeps 0.1
ours "syn12_eps01_r1" -m 4096 -n 4096 -d 128 -delta 4 -b 5 -s 1 $DP -dpeps 0.1
echo "done=$(date)" >> "$OUT/env.txt"
echo "$OUT"
