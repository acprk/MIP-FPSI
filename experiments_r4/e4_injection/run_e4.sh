#!/usr/bin/env bash
# E4: end-to-end content-injection attack on the CPython workload.
# Part A (full protocol, build10, every run checked against the plaintext): targets 0 and 1, both
#   hypotheses, exact mode; target 0 also rounded and DP+U-free (eps=1).
# Part B (leak-only, build11: the run stops right after the load is released, which is all the sender
#   sees that depends on Y by Theorem 4): all 10 targets x 2 hypotheses, exact and rounded once each,
#   DP+U-free (fixed cover, eps=1) 30 times each.
set -uo pipefail
cd "$(dirname "$0")"
FULL=../../impl/build10/net_bench
LEAK=../../impl/build11/net_bench
OUT=results_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
{ echo "start=$(date)"; sha256sum $FULL $LEAK; } > "$OUT/env.txt"
PORT=28000
run() { # nb tag data args...
  local nb=$1 tag=$2 data=$3; shift 3
  PORT=$((PORT + 1)); [ $PORT -gt 28900 ] && PORT=28001
  taskset -c 41 $nb -role 1 -addr 127.0.0.1:$PORT -indexverify -data "$data" -d 64 -delta 3 -b 4 -s 1 -nofallback "$@" > "$OUT/$tag.recv.log" 2>&1 &
  local rp=$!; sleep 0.2
  taskset -c 40 $nb -role 0 -addr 127.0.0.1:$PORT -data "$data" -d 64 -delta 3 -b 4 -s 1 -nofallback "$@" > "$OUT/$tag.send.log" 2>&1
  local se=$?; wait $rp; local re=$?
  echo "$tag se=$se re=$re observed$(grep -o ' B=[0-9]*' "$OUT/$tag.send.log" | head -1) $(grep -o 'false_neg=[0-9]* false_pos=[0-9]*' "$OUT/$tag.recv.log")" >> "$OUT/summary.txt"
}
# Part B first (fast)
for t in 0 1 2 3 4 5 6 7 8 9; do
  for h in H0 H1; do
    d=corpus/target$t/$h.bin
    run $LEAK "B_t${t}_${h}_exact" $d -leakonly
    run $LEAK "B_t${t}_${h}_pow2" $d -leakonly -pow2
    for r in $(seq 1 30); do run $LEAK "B_t${t}_${h}_dp_r$r" $d -leakonly -noU -dpeps 1; done
  done
done
# Part A
for t in 0 1; do
  for h in H0 H1; do run $FULL "A_t${t}_${h}_exact" corpus/target$t/$h.bin; done
done
for h in H0 H1; do
  run $FULL "A_t0_${h}_pow2" corpus/target0/$h.bin -pow2
  run $FULL "A_t0_${h}_dp" corpus/target0/$h.bin -noU -dpeps 1
done
echo "done=$(date)" >> "$OUT/env.txt"
echo "$OUT"
