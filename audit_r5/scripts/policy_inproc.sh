#!/usr/bin/env bash
# The cases of impl/scripts/test_policies.sh, run with fpsi_bench (both parties in one process,
# LocalAsyncSocket) because loopback TCP is blocked in the audit environment. Sizes <= 2^10.
set -uo pipefail
AUD=<ARTIFACT_ROOT>/audit_r5
IMPL=<ARTIFACT_ROOT>/impl
FB=$AUD/build/fpsi_bench
OUT=$AUD/logs/policy_inproc
mkdir -p "$OUT"
python3 $IMPL/scripts/make_policy_data.py "$OUT/clustered.bin" > /dev/null
: > "$OUT/summary.txt"
fails=0
run() { # run <name> <ok|abort> <pattern> <args...>
  local name=$1 expect=$2 pat=$3; shift 3
  timeout 120 taskset -c 44,45 $FB -indexverify "$@" > "$OUT/$name.log" 2>&1
  local ec=$? v=PASS
  if [ "$ec" = 124 ]; then v="FAIL(hang)"
  elif [ "$expect" = ok ]; then { [ "$ec" = 0 ] && grep -q "false_neg=0 false_pos=0" "$OUT/$name.log"; } || v=FAIL
  else [ "$ec" != 0 ] || v=FAIL; fi
  if [ -n "$pat" ] && ! grep -q "$pat" "$OUT/$name.log"; then v="$v(no '$pat')"; fi
  [ "$v" = PASS ] || fails=$((fails + 1))
  echo "$name: expect=$expect exit=$ec -> $v" | tee -a "$OUT/summary.txt"
}
run recv_overflow abort "B exceeds" -d 64 -delta 3 -b 4 -s 1 -m 1024 -n 1024 -Bbar 1 -nofallback
run send_overflow_index abort "U exceeds" -data "$OUT/clustered.bin" -d 128 -delta 4 -b 5 -s 1 -Bbar 4 -Ubar 1 -nofallback
run send_overflow_fallback ok "fallback=1" -data "$OUT/clustered.bin" -d 128 -delta 4 -b 5 -s 1 -Bbar 64 -Ubar 1
run bounds_ok ok "fallback=0" -data "$OUT/clustered.bin" -d 128 -delta 4 -b 5 -s 1 -Bbar 4 -Ubar 4 -nofallback
run dp_plus_bound abort "exclusive" -d 128 -delta 4 -b 5 -s 1 -m 256 -n 256 -Bbar 4 -dpeps 1 -nofallback
MIP_INJECT_CUCKOO_FAIL=3 run cuckoo_retry ok "" -d 128 -delta 4 -b 5 -s 1 -m 1024 -n 1024 -nofallback
MIP_INJECT_CUCKOO_FAIL=3 run cuckoo_retry_noU ok "" -d 128 -delta 4 -b 5 -s 1 -m 1024 -n 1024 -noU -pow2 -nofallback
run empty_sender ok "B=0" -data "$OUT/empty_sender.bin" -d 128 -delta 4 -b 5 -s 1
run empty_receiver ok "B=0" -data "$OUT/empty_receiver.bin" -d 128 -delta 4 -b 5 -s 1
MIP_INJECT_CUCKOO_FAIL=16 run cuckoo_hook_control abort "failed 16 times" -d 128 -delta 4 -b 5 -s 1 -m 1024 -n 1024 -nofallback
echo "failures=$fails" | tee -a "$OUT/summary.txt"
