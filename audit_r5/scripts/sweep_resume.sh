#!/usr/bin/env bash
# Resume of sweep.sh after the session was interrupted (11-10): same cases, d=128 delta=8 only,
# skipping (file, mode) pairs already present in logs/sweep/summary.tsv. Appends to that summary.
set -uo pipefail
AUD=<ARTIFACT_ROOT>/audit_r5
FB=$AUD/build/fpsi_bench
DATA=$AUD/data
OUT=$AUD/logs/sweep
SUM=$OUT/summary.tsv
sed -i '/^pass=/d' "$SUM"

run() {
  local f=$1 mode=$2; shift 2
  if grep -qP "^(PASS|FAIL)\t$f\t$mode\t" "$SUM"; then return; fi
  local log="$OUT/${f}__${mode}.log"
  timeout 600 taskset -c 44,45 $FB -data "$DATA/$f.bin" -indexverify "$@" > "$log" 2>&1
  local ec=$?
  local hdr res v=FAIL
  hdr=$(grep -h "^m=" "$log" | head -n 1)
  res=$(grep -h "^matches=" "$log")
  if [ "$ec" = 0 ] && echo "$res" | grep -q "false_neg=0 false_pos=0"; then v=PASS; fi
  printf "%s\t%s\t%s\texit=%s\t%s\t%s\targs=%s\n" "$v" "$f" "$mode" "$ec" "$hdr" "$res" "$*" >> "$SUM"
}

d=128; t=8; tag="d${d}_t${t}"; b=9; b1=5; cq=2
P="-d $d -delta $t -b $b -s 1"
for kind in boundary clusterR clusterS clusterRS dups mlt mgt big one_eq one_ne; do
  f="${kind}_${tag}"
  case $kind in clusterR|clusterS|clusterRS) BB="-Bbar 208 -Ubar 256" ;; *) BB="-Bbar 32 -Ubar 32" ;; esac
  run $f exact          $P
  run $f pow2           $P -pow2
  run $f noU            $P -noU
  run $f noU_pow2       $P -noU -pow2
  run $f dp             $P -dpeps 1
  run $f dp_noU         $P -dpeps 1 -noU
  run $f dp_pow2        $P -dpeps 0.5 -pow2
  run $f bounds         $P $BB
  run $f idx            $P -nofallback
  run $f idx_noU        $P -nofallback -noU
  run $f idx_noU_dp     $P -nofallback -noU -dpeps 1 -pow2
  run $f idx_pow2       $P -nofallback -pow2
  run $f idx_bounds     $P -nofallback $BB
  run $f idx_otthr      $P -nofallback -otthreshold
  run $f idx_nocomp     $P -nofallback -nocompress
  run $f allpairs       $P -allpairs
  case $kind in clusterR|clusterS|clusterRS) ;; *)
    run $f idx_r1     -d $d -delta $t -b $b1 -s 1 -nofallback
    run $f idx_r1_noU -d $d -delta $t -b $b1 -s 1 -nofallback -noU
    run $f idx_cover  -d $d -delta $t -cover $cq -nofallback
    ;;
  esac
done
for kind in emptyX emptyY emptyXY; do
  f="${kind}_${tag}"
  run $f exact   $P
  run $f noU     $P -noU
  run $f bounds  $P -Bbar 1 -Ubar 1 -nofallback
done
echo "pass=$(grep -c '^PASS' "$SUM") fail=$(grep -c '^FAIL' "$SUM")" | tee -a "$SUM"
