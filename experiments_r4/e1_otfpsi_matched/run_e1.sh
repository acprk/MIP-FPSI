#!/usr/bin/env bash
# E1: integrated output-matched otFPSI vs our protocol, paired and randomized.
# 5 rounds; in each round, in random order: output-matched otFPSI (SoftSpoken) at 2^12 (d=128,
# delta=4) and on CPython (d=64, delta=3), and our exact / rounded / U-free modes on the same inputs.
# All runs verified against the plaintext. Cores 46,47.
set -uo pipefail
cd "$(dirname "$0")"
if ps -C otfpsi,net_bench,fpsi_bench,or_deliver -o pid= | read -r _; then
  echo "another benchmark is running; refusing to start" >&2; exit 1
fi
NB=../../impl/build10/net_bench
OUT=results_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
python3 gen_synth.py 4096 4096 128 4 12 "$OUT/syn12.bin"
CODE=code_dedup.bin   # CPython fingerprints, deduplicated (our protocol deduplicates locally)
OT=../../baselines/others/xdup/01_otfpsi_cpu/build/otfpsi
native() { # tag m n d : native otFPSI (pairwise output) on random inputs of the same size
  /usr/bin/time -f "%e %U %S" -o "$OUT/$1.time" taskset -c 46,47 $OT -proto_distance batched -proto_threshold np \
     -querier_size $2 -responder_size $3 -vector_size $4 -threshold 4 -prf aes -ot softspoken \
     -localhost:1212 127.0.0.1:$((PORT + 1)) > "$OUT/$1.log" 2>&1
  echo "$1 exit=$?" >> "$OUT/summary.txt"
}
{ echo "start=$(date)"; cat /proc/loadavg; sha256sum $NB ../../impl/build10/or_deliver ../../baselines/others/xdup/01_otfpsi_cpu/build/otfpsi "$OUT/syn12.bin" $CODE; } > "$OUT/env.txt"
PORT=26000
ours() { # tag args...
  local tag=$1; shift
  PORT=$((PORT + 2))
  taskset -c 47 $NB -role 1 -addr 127.0.0.1:$PORT -indexverify "$@" > "$OUT/$tag.recv.log" 2>&1 &
  local rp=$!; sleep 0.3
  taskset -c 46 $NB -role 0 -addr 127.0.0.1:$PORT "$@" > "$OUT/$tag.send.log" 2>&1
  local se=$?; wait $rp; local re=$?
  echo "$tag sender_exit=$se receiver_exit=$re load=$(cut -d' ' -f1 /proc/loadavg)" >> "$OUT/summary.txt"
}
job() {
  case $1 in
    m12)        PORT=$((PORT + 2)); bash run_matched.sh "$OUT/syn12.bin" 128 4 softspoken "$OUT/m12_r$k" $PORT
                echo "m12_r$k $(tr '\n' ' ' < "$OUT/m12_r$k.status")" >> "$OUT/summary.txt" ;;
    mcode)      PORT=$((PORT + 2)); bash run_matched.sh $CODE 64 3 softspoken "$OUT/mcode_r$k" $PORT
                echo "mcode_r$k $(tr '\n' ' ' < "$OUT/mcode_r$k.status")" >> "$OUT/summary.txt" ;;
    n12)        PORT=$((PORT + 2)); native "n12_r$k" 4096 4096 128 ;;
    ncode)      PORT=$((PORT + 2)); native "ncode_r$k" 2973 2959 64 ;;
    o12_exact)  ours "o12_exact_r$k" -data "$OUT/syn12.bin" -d 128 -delta 4 -b 5 -s 1 ;;
    o12_pow2)   ours "o12_pow2_r$k" -data "$OUT/syn12.bin" -d 128 -delta 4 -b 5 -s 1 -pow2 ;;
    o12_noU)    ours "o12_noU_r$k" -data "$OUT/syn12.bin" -d 128 -delta 4 -b 5 -s 1 -pow2 -noU ;;
    oc_exact)   ours "oc_exact_r$k" -data $CODE -d 64 -delta 3 -b 4 -s 1 ;;
    oc_pow2)    ours "oc_pow2_r$k" -data $CODE -d 64 -delta 3 -b 4 -s 1 -pow2 ;;
    oc_noU)     ours "oc_noU_r$k" -data $CODE -d 64 -delta 3 -b 4 -s 1 -pow2 -noU ;;
  esac
}
JOBS="m12 mcode n12 ncode o12_exact o12_pow2 o12_noU oc_exact oc_pow2 oc_noU"
for k in 1 2 3 4 5; do
  order=$(printf '%s\n' $JOBS | shuf)
  echo "round $k order: $(echo $order)" >> "$OUT/env.txt"
  for j in $order; do job "$j"; done
done
echo "done=$(date)" >> "$OUT/env.txt"
echo "$OUT"
