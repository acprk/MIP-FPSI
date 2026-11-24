#!/usr/bin/env bash
# Fair head-to-head with otFPSI (xDup, S&P'26; artifact Zenodo 10.5281/zenodo.19480020, BSD-3).
# Same host, same two pinned physical cores, interleaved runs. otFPSI runs its two parties as two
# threads over loopback TCP (its default); ours runs both parties in one thread. We record wall
# time and user+sys CPU time for both with /usr/bin/time, plus communication from each tool.
set -euo pipefail
cd "$(dirname "$0")/.."
CORES="${1:-38,39}"
OT=../baselines/others/xdup/01_otfpsi/build/otfpsi
OURS=./build2/fpsi_bench
OUT=../results/vs_otfpsi_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
{ echo "cores=$CORES start=$(date)"; cat /proc/loadavg; } > "$OUT/env.txt"
TF="%e %U %S"

one() { # tag cmd...
  local tag=$1; shift
  /usr/bin/time -f "$TF" -o "$OUT/$tag.time" taskset -c "$CORES" "$@" > "$OUT/$tag.log" 2>&1 || echo FAILED >> "$OUT/$tag.log"
  echo "$tag $(cat "$OUT/$tag.time") load=$(cut -d' ' -f1 /proc/loadavg)" >> "$OUT/summary.txt"
}

for delta in 4 16; do
  if [ "$delta" = 4 ]; then BS="-b 5 -s 1"; else BS="-b 9 -s 1"; fi
  for lg in 8 10 12 14; do
    n=$((1 << lg)); reps=3; [ "$lg" = 14 ] && reps=1
    for k in $(seq 1 $reps); do
      one "otfpsi_d128_delta${delta}_2e${lg}_rep$k" $OT -proto_distance batched -proto_threshold np \
          -querier_size $n -responder_size $n -vector_size 128 -threshold $delta -prf aes -ot silent
      one "ours_d128_delta${delta}_2e${lg}_rep$k" $OURS -m $n -n $n -d 128 -delta $delta $BS -noverify
    done
  done
done
echo "done=$(date)" >> "$OUT/env.txt"
