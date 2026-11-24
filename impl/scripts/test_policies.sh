#!/usr/bin/env bash
# Policy and fault tests for net_bench (build7): public-bound overflow on each side,
# overflow combined with the all-pairs fallback, conflicting DP + bound options,
# and injected cuckoo-insertion failures. Every case must terminate (timeout 300 s).
set -uo pipefail
cd "$(dirname "$0")/.."
NB=./build9/net_bench
OUT=../results/policy_tests_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
python3 scripts/make_policy_data.py "$OUT/clustered.bin" > /dev/null
PORT=23500
fails=0

# run <name> <expect: ok|abort> <grep-pattern-or-empty> <extra args...>
run() {
  local name=$1 expect=$2 pat=$3; shift 3
  PORT=$((PORT + 1))
  timeout 300 taskset -c 47 $NB -role 1 -addr 127.0.0.1:$PORT -indexverify "$@" > "$OUT/$name.recv.log" 2>&1 &
  local rp=$!; sleep 0.3
  timeout 300 taskset -c 46 $NB -role 0 -addr 127.0.0.1:$PORT "$@" > "$OUT/$name.send.log" 2>&1
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

# 1. receiver overflow: 4096 uniform vectors on 16-bit keys give B >= 2 > Bbar
run recv_overflow abort "B exceeds" -d 64 -delta 3 -b 4 -s 1 -m 4096 -n 4096 -Bbar 1 -nofallback
# 2. sender overflow in the index branch (rows 0, 1 share keys, so U >= 2 > Ubar)
run send_overflow_index abort "U exceeds" -data "$OUT/clustered.bin" -d 128 -delta 4 -b 5 -s 1 -Bbar 4 -Ubar 1 -nofallback
# 3. same inputs, large Bbar: the fallback is taken, U is not checked (bounded functionality)
run send_overflow_fallback ok "fallback=1" -data "$OUT/clustered.bin" -d 128 -delta 4 -b 5 -s 1 -Bbar 64 -Ubar 1
# 4. control: bounds respected, index branch
run bounds_ok ok "fallback=0" -data "$OUT/clustered.bin" -d 128 -delta 4 -b 5 -s 1 -Bbar 4 -Ubar 4 -nofallback
# 5. conflicting policies are rejected
run dp_plus_bound abort "exclusive" -d 128 -delta 4 -b 5 -s 1 -m 256 -n 256 -Bbar 4 -dpeps 1 -nofallback
# 6. injected cuckoo failures are retried locally with fresh seeds
MIP_INJECT_CUCKOO_FAIL=3 run cuckoo_retry ok "" -d 128 -delta 4 -b 5 -s 1 -m 1024 -n 1024 -nofallback
MIP_INJECT_CUCKOO_FAIL=3 run cuckoo_retry_noU ok "" -d 128 -delta 4 -b 5 -s 1 -m 1024 -n 1024 -noU -pow2 -nofallback
# 8. empty inputs end after the size exchange (no load released); a wrong -d is rejected
run empty_sender ok "B=0" -data "$OUT/empty_sender.bin" -d 128 -delta 4 -b 5 -s 1
run empty_receiver ok "B=0" -data "$OUT/empty_receiver.bin" -d 128 -delta 4 -b 5 -s 1
run wrong_dim abort "header does not match" -data "$OUT/clustered.bin" -d 64 -delta 3 -b 4 -s 1
# 7. control that the hook fires: 16 injected failures exhaust the retry budget
MIP_INJECT_CUCKOO_FAIL=16 run cuckoo_hook_control abort "failed 16 times" -d 128 -delta 4 -b 5 -s 1 -m 1024 -n 1024 -nofallback

echo "failures=$fails" | tee -a "$OUT/summary.txt"
echo "$OUT"
[ "$fails" = 0 ]
