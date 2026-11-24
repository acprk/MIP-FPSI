#!/usr/bin/env bash
# Round-B extras (CPU-time metric; cores 44,45 so they do not disturb campaign3 on 38,39):
#  * output-matched otFPSI baselines: otFPSI-ssb (shared_batched) and otFPSI-ss (individual) with
#    secret-shared outputs, 2^10 (3 reps) and 2^12 (1 rep, ssb only)
#  * data-seed variation: our protocol, rounded mode, d=128, delta in {4,8}, 2^12, seeds 1..5
# Usage: scripts/run_c3b_extra.sh
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=../results/c3b_extra_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
OT=../baselines/others/xdup/01_otfpsi/build/otfpsi
NB=./build5/net_bench
CG=/sys/fs/cgroup/user.slice/user-1000.slice/cpu.stat
thr() { grep nr_throttled "$CG" | cut -d' ' -f2; }
echo "start=$(date)" > "$OUT/env.txt"
otf() { # tag n dist
  local tag=$1 n=$2 dist=$3 t0; t0=$(thr)
  /usr/bin/time -f "%e %U %S" -o "$OUT/$tag.time" taskset -c 44,45 $OT -proto_distance $dist -proto_threshold np -secret_shared \
     -querier_size $n -responder_size $n -vector_size 128 -threshold 4 -prf aes -ot softspoken > "$OUT/$tag.log" 2>&1
  echo "$tag thr=$(( $(thr) - t0 )) $(cat "$OUT/$tag.time") $(grep -h 'Total Communication' "$OUT/$tag.log" | head -1)" >> "$OUT/summary.txt"
}
PORT=2600
ours() {
  local tag=$1; shift; PORT=$((PORT + 1))
  taskset -c 45 $NB -role 1 -addr 127.0.0.1:$PORT -indexverify "$@" > "$OUT/$tag.recv.log" 2>&1 &
  local rp=$!; sleep 0.3
  taskset -c 44 $NB -role 0 -addr 127.0.0.1:$PORT "$@" > "$OUT/$tag.send.log" 2>&1
  wait $rp
  echo "$tag $(grep -h -E 'B=|wall_s|matches' "$OUT/$tag.recv.log" "$OUT/$tag.send.log" | tr '\n' ' ')" >> "$OUT/summary.txt"
}
for k in 1 2 3; do
  otf "ssb_2e10_r$k" 1024 shared_batched
  otf "ss_2e10_r$k" 1024 individual
done
for seed in 1 2 3 4 5; do
  ours "seed${seed}_d128_t4_2e12_pow2" -m 4096 -n 4096 -d 128 -delta 4 -b 5 -s 1 -pow2 -seed $seed
  ours "seed${seed}_d128_t8_2e12_pow2" -m 4096 -n 4096 -d 128 -delta 8 -b 9 -s 1 -pow2 -seed $seed
done
otf "ssb_2e12_r1" 4096 shared_batched
echo "done=$(date)" >> "$OUT/env.txt"
