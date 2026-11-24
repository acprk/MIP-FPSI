#!/usr/bin/env bash
# Campaign 4: paired, randomized head-to-head (answers "competitors not interleaved").
# Each repetition runs otFPSI (SoftSpoken, its faster mode) and our protocol (build6: arbitrary-size
# Benes networks) in exact, rounded and U-free mode for delta in {4,8} at m=n=2^12, d=128, in a
# random order. Same two physical cores (38,39) for both protocols. Primary metric: CPU seconds of
# both parties; otFPSI additionally reports its own protocol-scoped "Total Time" in its log.
# Usage: scripts/run_campaign4_paired.sh
set -uo pipefail
cd "$(dirname "$0")/.."
if ps -C otfpsi,net_bench,fpsi_bench -o pid= | read -r _; then
  echo "another benchmark is running; refusing to start" >&2; exit 1
fi
NB=./build6/net_bench
OT=../baselines/others/xdup/01_otfpsi/build/otfpsi
CG=/sys/fs/cgroup/user.slice/user-1000.slice/cpu.stat
OUT=../results/campaign4_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
{ echo "start=$(date)"; cat /proc/loadavg; sha256sum $NB $OT; } > "$OUT/env.txt"
gate() { while [ "$(cut -d' ' -f1 /proc/loadavg | cut -d. -f1)" -ge 30 ]; do sleep 60; done; }
thr() { grep nr_throttled "$CG" | cut -d' ' -f2; }
PORT=23000 # high, unused range (port 3001 is taken by another service on this host)
ours() { # tag args...
  local tag=$1; shift; gate; local t0; t0=$(thr); PORT=$((PORT + 1))
  taskset -c 39 $NB -role 1 -addr 127.0.0.1:$PORT -indexverify "$@" > "$OUT/$tag.recv.log" 2>&1 &
  local rp=$!; sleep 0.3
  taskset -c 38 $NB -role 0 -addr 127.0.0.1:$PORT "$@" > "$OUT/$tag.send.log" 2>&1
  wait $rp
  echo "$tag thr=$(( $(thr) - t0 )) $(grep -h -E 'wall_s|matches' "$OUT/$tag.recv.log" "$OUT/$tag.send.log" | tr '\n' ' ')" >> "$OUT/summary.txt"
}
otf() { # tag
  local tag=$1; gate; local t0; t0=$(thr)
  /usr/bin/time -f "%e %U %S" -o "$OUT/$tag.time" taskset -c 38,39 $OT -proto_distance batched -proto_threshold np \
     -querier_size 4096 -responder_size 4096 -vector_size 128 -threshold 4 -prf aes -ot softspoken > "$OUT/$tag.log" 2>&1
  echo "$tag thr=$(( $(thr) - t0 )) $(cat "$OUT/$tag.time") $(grep -h 'Total Time' "$OUT/$tag.log" | tail -1)" >> "$OUT/summary.txt"
}
for k in 1 2 3 4 5; do
  jobs=(ot d4_exact d4_pow2 d4_noU d8_exact d8_pow2 d8_noU)
  for j in $(printf '%s\n' "${jobs[@]}" | shuf); do
    case $j in
      ot) otf "ot_soft_r$k" ;;
      d4_exact) ours "d4_exact_r$k" -m 4096 -n 4096 -d 128 -delta 4 -b 5 -s 1 ;;
      d4_pow2) ours "d4_pow2_r$k" -m 4096 -n 4096 -d 128 -delta 4 -b 5 -s 1 -pow2 ;;
      d4_noU) ours "d4_noU_r$k" -m 4096 -n 4096 -d 128 -delta 4 -b 5 -s 1 -pow2 -noU ;;
      d8_exact) ours "d8_exact_r$k" -m 4096 -n 4096 -d 128 -delta 8 -b 9 -s 1 ;;
      d8_pow2) ours "d8_pow2_r$k" -m 4096 -n 4096 -d 128 -delta 8 -b 9 -s 1 -pow2 ;;
      d8_noU) ours "d8_noU_r$k" -m 4096 -n 4096 -d 128 -delta 8 -b 9 -s 1 -pow2 -noU ;;
    esac
  done
done
echo "done=$(date)" >> "$OUT/env.txt"
