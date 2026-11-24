#!/usr/bin/env bash
# E4 driver: (a) receiver clusters, (b) m/n sweep. d=128, delta=4, block cover (5,1).
# Modes: exact (no flag), rounded (-pow2), U-free (-pow2 -noU, as in the paper's campaigns).
# One run at a time, cores 44,45 (run1.sh), /usr/bin/time -v, loadavg + nr_throttled recorded.
# Resumable: a run whose log already has an "# end" line is skipped.
set -uo pipefail
R=<ARTIFACT_ROOT>/experiments_r6
E=$R/e4_stress
RUN=$R/run1.sh
LOG=$E/logs
mkdir -p "$LOG"
P="-d 128 -delta 4 -b 5 -s 1 -indexverify"
# 100 GiB address-space cap per run (protects the shared host); exceeding it is reported.
CAP=107374182400

one() { # one <tag> <args...>
  local tag=$1; shift
  local log=$LOG/$tag.log
  if [ -f "$log" ] && grep -q "^# end" "$log"; then return; fi
  echo "$(date -Is) start $tag"
  local ec
  ec=$(prlimit --as=$CAP bash $RUN "$log" 7200 "$@")
  echo "$(date -Is) done $tag exit=$ec"
}

mode_flags() {
  case $1 in exact) echo "" ;; rounded) echo "-pow2" ;; ufree) echo "-pow2 -noU" ;; esac
}

part_a() { # part_a <k list> <reps>
  local reps=$1; shift
  for k in "$@"; do
    for rep in $(seq 1 "$reps"); do
      for md in exact rounded ufree; do
        one "a_k${k}_${md}_r${rep}" -data "$E/data/cluster_k$k.bin" $P $(mode_flags $md)
      done
    done
  done
}

part_b() {
  for m in 256 1024 4096 16384 65536; do
    reps=3; [ "$m" = 65536 ] && reps=1
    for rep in $(seq 1 $reps); do
      for md in exact ufree; do
        one "b_m${m}_n4096_${md}_r${rep}" -m $m -n 4096 $P $(mode_flags $md)
      done
    done
  done
  for n in 256 1024 16384 65536; do
    reps=3; [ "$n" = 65536 ] && reps=1
    for rep in $(seq 1 $reps); do
      for md in exact ufree; do
        one "b_m4096_n${n}_${md}_r${rep}" -m 4096 -n $n $P $(mode_flags $md)
      done
    done
  done
}

case ${1:-all} in
  a_small) part_a 3 1 2 4 8 16 32 64 ;;
  a_large) part_a 1 128 512 2048 ;;
  b) part_b ;;
  all) part_a 3 1 2 4 8 16 32 64; part_b; part_a 1 128 512 2048 ;;
esac
echo "$(date -Is) finished ${1:-all}"
