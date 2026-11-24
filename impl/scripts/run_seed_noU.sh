#!/usr/bin/env bash
# Data-seed variation for the U-free mode (d=128, 2^12, delta 4 and 8, seeds 1..5), cores 46,47.
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=../results/c3b_extra_20261024_0116
NB=./build5/net_bench
PORT=2700
for seed in 1 2 3 4 5; do
  for dl in 4 8; do
    b=5; [ $dl = 8 ] && b=9
    tag="seed${seed}_d128_t${dl}_2e12_noU"; PORT=$((PORT + 1))
    taskset -c 47 $NB -role 1 -addr 127.0.0.1:$PORT -indexverify -m 4096 -n 4096 -d 128 -delta $dl -b $b -s 1 -pow2 -noU -seed $seed > "$OUT/$tag.recv.log" 2>&1 &
    rp=$!; sleep 0.3
    taskset -c 46 $NB -role 0 -addr 127.0.0.1:$PORT -m 4096 -n 4096 -d 128 -delta $dl -b $b -s 1 -pow2 -noU -seed $seed > "$OUT/$tag.send.log" 2>&1
    wait $rp
  done
done
echo "seed noU done $(date)" >> "$OUT/env.txt"
