#!/usr/bin/env bash
# Copy of impl/scripts/test_policies.sh for audit_r5: points at the fresh build in audit_r5/build,
# writes logs under audit_r5/logs/policy, and pins both parties to cores 44,45.
# Test cases and expectations are unchanged.
set -uo pipefail
AUD=<ARTIFACT_ROOT>/audit_r5
IMPL=<ARTIFACT_ROOT>/impl
NB=$AUD/build/net_bench
OUT=$AUD/logs/policy
mkdir -p "$OUT"
python3 $IMPL/scripts/make_policy_data.py "$OUT/clustered.bin" > /dev/null
PORT=23700
fails=0

# run <name> <expect: ok|abort> <grep-pattern-or-empty> <extra args...>
run() {
  local name=$1 expect=$2 pat=$3; shift 3
  PORT=$((PORT + 1))
  timeout 300 taskset -c 45 $NB -role 1 -addr 127.0.0.1:$PORT -indexverify "$@" > "$OUT/$name.recv.log" 2>&1 &
  local rp=$!; sleep 0.3
  timeout 300 taskset -c 44 $NB -role 0 -addr 127.0.0.1:$PORT "$@" > "$OUT/$name.send.log" 2>&1
  local se=$?
  wait $rp; local re=$?
  local verdict=PASS
  if [ "$se" = 124 ] || [ "$re" = 124 ]; then verdict="FAIL(hang)"
  elif [ "$expect" = ok ]; then
    { [ "$se" = 0 ] && [ "$re" = 0 ] && grep -q "false_neg=0 false_pos=0" "$OUT/$name.recv.log"; } || verdict=FAIL
  else
    { [ "$se" != 0 ] || [ "$re" != 0 ]; } || verdict=FAIL
  fi
  if [ -n "$pat" ] && ! cat "$OUT/$name.send.log" "$OUT/$name.recv.log" | grep -q "$pat"; then verdict="FAIL(no '$pat')"; fi
  [ "$verdict" = PASS ] || fails=$((fails + 1))
  echo "$name: expect=$expect sender_exit=$se receiver_exit=$re -> $verdict" | tee -a "$OUT/summary.txt"
}

: > "$OUT/summary.txt"
run recv_overflow abort "B exceeds" -d 64 -delta 3 -b 4 -s 1 -m 4096 -n 4096 -Bbar 1 -nofallback
run send_overflow_index abort "U exceeds" -data "$OUT/clustered.bin" -d 128 -delta 4 -b 5 -s 1 -Bbar 4 -Ubar 1 -nofallback
run send_overflow_fallback ok "fallback=1" -data "$OUT/clustered.bin" -d 128 -delta 4 -b 5 -s 1 -Bbar 64 -Ubar 1
run bounds_ok ok "fallback=0" -data "$OUT/clustered.bin" -d 128 -delta 4 -b 5 -s 1 -Bbar 4 -Ubar 4 -nofallback
run dp_plus_bound abort "exclusive" -d 128 -delta 4 -b 5 -s 1 -m 256 -n 256 -Bbar 4 -dpeps 1 -nofallback
MIP_INJECT_CUCKOO_FAIL=3 run cuckoo_retry ok "" -d 128 -delta 4 -b 5 -s 1 -m 1024 -n 1024 -nofallback
MIP_INJECT_CUCKOO_FAIL=3 run cuckoo_retry_noU ok "" -d 128 -delta 4 -b 5 -s 1 -m 1024 -n 1024 -noU -pow2 -nofallback
run empty_sender ok "B=0" -data "$OUT/empty_sender.bin" -d 128 -delta 4 -b 5 -s 1
run empty_receiver ok "B=0" -data "$OUT/empty_receiver.bin" -d 128 -delta 4 -b 5 -s 1
run wrong_dim abort "header does not match" -data "$OUT/clustered.bin" -d 64 -delta 3 -b 4 -s 1
MIP_INJECT_CUCKOO_FAIL=16 run cuckoo_hook_control abort "failed 16 times" -d 128 -delta 4 -b 5 -s 1 -m 1024 -n 1024 -nofallback

echo "failures=$fails" | tee -a "$OUT/summary.txt"
[ "$fails" = 0 ]
