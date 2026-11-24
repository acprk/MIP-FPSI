#!/usr/bin/env bash
# E5: the 11 cases of impl/scripts/test_policies.sh, in-process (fpsi_bench, build12 with the
# abort-propagating harness), same arguments and expected outcomes. One run at a time, cores 44,45.
set -uo pipefail
R=<ARTIFACT_ROOT>
E=$R/experiments_r6/e5_abort
RUN=$R/experiments_r6/run1.sh
OUT=$E/logs
mkdir -p "$OUT"
python3 $R/impl/scripts/make_policy_data.py "$OUT/clustered.bin" > /dev/null
CSV=$E/policies.csv
echo "case,expect,exit,pattern,pattern_found,abort_line,verdict,wall_s,maxrss_kb" > "$CSV"
fails=0
run() { # run <name> <ok|abort> <pattern> <args...>
  local name=$1 expect=$2 pat=$3; shift 3
  local log=$OUT/$name.log
  local ec
  ec=$(bash $RUN "$log" 300 -indexverify "$@")
  local v=PASS
  if [ "$ec" = 124 ]; then v="FAIL(hang)"
  elif [ "$expect" = ok ]; then { [ "$ec" = 0 ] && grep -q "false_neg=0 false_pos=0" "$log"; } || v=FAIL
  else [ "$ec" != 0 ] || v=FAIL; fi
  local pf=yes
  if [ -n "$pat" ] && ! grep -q "$pat" "$log"; then pf=no; v="$v(no '$pat')"; fi
  [ -z "$pat" ] && pf=n/a
  [ "$v" = PASS ] || fails=$((fails + 1))
  local ab wall rss
  ab=$(grep -h "^abort:" "$log" | tr '\n' ';' | tr ',' ' ')
  wall=$(grep -h "Elapsed (wall clock)" "$log" | awk '{print $NF}')
  rss=$(grep -h "Maximum resident" "$log" | awk '{print $NF}')
  echo "$name,$expect,$ec,$pat,$pf,$ab,$v,$wall,$rss" >> "$CSV"
  echo "$name: expect=$expect exit=$ec -> $v"
}
run recv_overflow abort "B exceeds" -d 64 -delta 3 -b 4 -s 1 -m 4096 -n 4096 -Bbar 1 -nofallback
run send_overflow_index abort "U exceeds" -data "$OUT/clustered.bin" -d 128 -delta 4 -b 5 -s 1 -Bbar 4 -Ubar 1 -nofallback
run send_overflow_fallback ok "fallback=1" -data "$OUT/clustered.bin" -d 128 -delta 4 -b 5 -s 1 -Bbar 64 -Ubar 1
run bounds_ok ok "fallback=0" -data "$OUT/clustered.bin" -d 128 -delta 4 -b 5 -s 1 -Bbar 4 -Ubar 4 -nofallback
run dp_plus_bound abort "exclusive" -d 128 -delta 4 -b 5 -s 1 -m 256 -n 256 -Bbar 4 -dpeps 1 -nofallback
export MIP_INJECT_CUCKOO_FAIL=3
run cuckoo_retry ok "" -d 128 -delta 4 -b 5 -s 1 -m 1024 -n 1024 -nofallback
run cuckoo_retry_noU ok "" -d 128 -delta 4 -b 5 -s 1 -m 1024 -n 1024 -noU -pow2 -nofallback
unset MIP_INJECT_CUCKOO_FAIL
run empty_sender ok "B=0" -data "$OUT/empty_sender.bin" -d 128 -delta 4 -b 5 -s 1
run empty_receiver ok "B=0" -data "$OUT/empty_receiver.bin" -d 128 -delta 4 -b 5 -s 1
run wrong_dim abort "header does not match" -data "$OUT/clustered.bin" -d 64 -delta 3 -b 4 -s 1
export MIP_INJECT_CUCKOO_FAIL=16
run cuckoo_hook_control abort "failed 16 times" -d 128 -delta 4 -b 5 -s 1 -m 1024 -n 1024 -nofallback
unset MIP_INJECT_CUCKOO_FAIL
echo "failures=$fails"
