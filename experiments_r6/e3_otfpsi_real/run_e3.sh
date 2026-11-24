#!/usr/bin/env bash
# E3: otFPSI (xDup, S&P'26) on the EXACT deduplicated real fingerprints, in-process transport.
# One job per invocation; jobs are run strictly one after another on cores 42,43.
#   run_e3.sh native  <tag> <data.bin> <delta> <ot>   native pairwise-output otFPSI, real-input path
#   run_e3.sh matched <tag> <data.bin> <delta> <ot>   output-matched: stage 1 (shuffle + shared output)
#                                                     + stage 2 (OR + OT delivery, in-process)
#   run_e3.sh artifact <tag> <m> <n> <d> <delta> <ot> the artifact's own native benchmark (random data)
# Every job records /proc/loadavg, cgroup nr_throttled and the date before and after, and
# /usr/bin/time -v (user+sys CPU, max RSS) per process.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OT=$HERE/build_otfpsi/otfpsi
ORD=$HERE/build_stage2/or_deliver_local
OUT=$HERE/results
CG=/sys/fs/cgroup/user.slice/user-1000.slice/cpu.stat
CORES=42,43
mkdir -p "$OUT"
# refuse if another otfpsi / stage-2 run is active (ours or anyone's); other benchmarks (e.g. a
# parallel agent's fpsi_bench on other cores) are only logged, with their CPU affinity
if ps -C otfpsi,or_deliver,or_deliver_loca -o pid= | read -r _; then
  echo "another otfpsi/or_deliver run is active; refusing to start" >&2; exit 1
fi
others() { ps -C otfpsi,net_bench,fpsi_bench,or_deliver,or_deliver_loca -o pid=,psr=,comm= | tr '\n' ';'; }
thr() { grep nr_throttled "$CG" | cut -d' ' -f2; }
stamp() { echo "$1 date=$(date -Is) loadavg=$(tr ' ' '_' < /proc/loadavg) nr_throttled=$(thr) others=[$(others)]"; }
job=$1 tag=$2; shift 2
L=$OUT/$tag
{ stamp before; sha256sum "$OT" "$ORD"; } > "$L.env"
case $job in
  native)
    DATA=$1 DELTA=$2 OTM=$3
    read -r M N < <(python3 "$HERE/hdr.py" "$DATA")
    env OTFPSI_REAL_DATA="$DATA" /usr/bin/time -v -o "$L.time" taskset -c $CORES "$OT" -proto_distance batched \
      -proto_threshold np -querier_size "$M" -responder_size "$N" -vector_size 64 -threshold "$DELTA" -prf aes \
      -ot "$OTM" > "$L.log" 2>&1
    echo "exit=$?" >> "$L.env" ;;
  matched)
    DATA=$1 DELTA=$2 OTM=$3
    read -r M N < <(python3 "$HERE/hdr.py" "$DATA")
    env OTFPSI_MATCHED_DATA="$DATA" OTFPSI_MATCHED_OUT="$L.shares" OTFPSI_MATCHED_SHUFFLED_DATA="$L.perm.bin" \
      /usr/bin/time -v -o "$L.s1.time" taskset -c $CORES "$OT" -proto_distance batched -proto_threshold np \
      -querier_size "$M" -responder_size "$N" -vector_size 64 -threshold "$DELTA" -prf aes -ot "$OTM" > "$L.s1.log" 2>&1
    echo "stage1 exit=$?" >> "$L.env"
    stamp between >> "$L.env"
    /usr/bin/time -v -o "$L.s2.time" taskset -c $CORES "$ORD" -data "$L.perm.bin" -shareS "$L.shares.S" \
      -shareR "$L.shares.R" -d 64 -delta "$DELTA" > "$L.s2.log" 2>&1
    echo "stage2 exit=$?" >> "$L.env"
    rm -f "$L.shares.S" "$L.shares.R" "$L.perm.bin" ;;
  artifact)
    M=$1 N=$2 D=$3 DELTA=$4 OTM=$5
    /usr/bin/time -v -o "$L.time" taskset -c $CORES "$OT" -proto_distance batched -proto_threshold np \
      -querier_size "$M" -responder_size "$N" -vector_size "$D" -threshold "$DELTA" -prf aes -ot "$OTM" > "$L.log" 2>&1
    echo "exit=$?" >> "$L.env" ;;
esac
stamp after >> "$L.env"
