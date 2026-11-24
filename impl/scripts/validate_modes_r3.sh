#!/usr/bin/env bash
# Correctness validation of the round-3 modes (build5): -noU, -dpeps 1, both; default for comm.
# Correctness only (times are not results: shared, loaded host). Four lanes on cores 44-47.
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=../results/modes_r3_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
FB=./build5/fpsi_bench
lane() { # core configs...
  local core=$1; shift
  for spec in "$@"; do
    local tag=${spec%%:*} args=${spec#*:}
    taskset -c "$core" $FB $args > "$OUT/$tag.log" 2>&1
    echo "$tag $(tr '\n' ' ' < "$OUT/$tag.log")" >> "$OUT/summary_$core.txt"
  done
}
C4="-d 128 -delta 4 -b 5 -s 1"
C8="-d 128 -delta 8 -b 9 -s 1"
CP="-data ../data/code_simhash64.bin -d 64 -delta 3 -b 4 -s 1"
lane 44 "def_c4_2e12:-m 4096 -n 4096 $C4" "noU_c4_2e10:-m 1024 -n 1024 $C4 -noU" "noU_c4_2e12:-m 4096 -n 4096 $C4 -noU" "dp_c4_2e10:-m 1024 -n 1024 $C4 -dpeps 1" &
lane 45 "dp_c4_2e12:-m 4096 -n 4096 $C4 -dpeps 1" "both_c4_2e10:-m 1024 -n 1024 $C4 -noU -dpeps 1" "both_c4_2e12:-m 4096 -n 4096 $C4 -noU -dpeps 1" "noU_cp:$CP -noU" &
lane 46 "noU_c8_2e10:-m 1024 -n 1024 $C8 -noU" "noU_c8_2e12:-m 4096 -n 4096 $C8 -noU" "dp_c8_2e10:-m 1024 -n 1024 $C8 -dpeps 1" "dp_cp:$CP -dpeps 1" &
lane 47 "dp_c8_2e12:-m 4096 -n 4096 $C8 -dpeps 1" "both_c8_2e10:-m 1024 -n 1024 $C8 -noU -dpeps 1" "both_c8_2e12:-m 4096 -n 4096 $C8 -noU -dpeps 1" "both_cp:$CP -noU -dpeps 1" "def_cp:$CP" &
wait
cat "$OUT"/summary_*.txt > "$OUT/summary.txt"
echo "$OUT"
