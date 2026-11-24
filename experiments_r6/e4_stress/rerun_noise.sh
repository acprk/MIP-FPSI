#!/usr/bin/env bash
# Second pass of E4(b) (repetitions r4-r6; r4 only for the 2^16 points), run after the first pass
# (r1-r3) showed up to 2x CPU spread at host loadavg ~27. Same conventions as run_e4.sh; existing
# logs are skipped.
R=<ARTIFACT_ROOT>/experiments_r6
E=$R/e4_stress
P="-d 128 -delta 4 -b 5 -s 1 -indexverify"
one() { # one <tag> <args...>
  local tag=$1; shift
  local log=$E/logs/$tag.log
  if [ -f "$log" ] && grep -q "^# end" "$log"; then return; fi
  echo "$(date -Is) $tag exit=$(prlimit --as=107374182400 bash $R/run1.sh "$log" 7200 "$@")"
}
for mn in "256 4096" "1024 4096" "4096 4096" "16384 4096" "65536 4096" "4096 256" "4096 1024" "4096 16384" "4096 65536"; do
  set -- $mn
  reps="4 5 6"; { [ "$1" = 65536 ] || [ "$2" = 65536 ]; } && reps="4"
  for rep in $reps; do
    one "b_m$1_n$2_exact_r$rep" -m $1 -n $2 $P
    one "b_m$1_n$2_ufree_r$rep" -m $1 -n $2 $P -pow2 -noU
  done
done
