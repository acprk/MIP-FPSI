#!/usr/bin/env bash
# Run one net_bench protocol execution (receiver on core 45, sender on core 44).
# usage: pair.sh <name> <port> <timeout_s> [net_bench args...]
# Logs: audit_r5/logs/<subdir from name>.{send,recv}.log ; prints one summary line.
set -uo pipefail
AUD=<ARTIFACT_ROOT>/audit_r5
NB=$AUD/build/net_bench
name=$1; port=$2; to=$3; shift 3
mkdir -p "$(dirname "$AUD/logs/$name")"
start=$(date +%s.%N)
timeout "$to" taskset -c 45 $NB -role 1 -addr 127.0.0.1:$port -indexverify "$@" > "$AUD/logs/$name.recv.log" 2>&1 &
rp=$!
sleep 0.5
timeout "$to" taskset -c 44 $NB -role 0 -addr 127.0.0.1:$port "$@" > "$AUD/logs/$name.send.log" 2>&1
se=$?
wait $rp; re=$?
end=$(date +%s.%N)
res=$(grep -h "matches=" "$AUD/logs/$name.recv.log" | tr '\n' ' ')
hdr=$(grep -h "^role=1" "$AUD/logs/$name.recv.log" | tr '\n' ' ')
err=$(grep -h -E "what\(\)|exceeds|exclusive|failed" "$AUD/logs/$name.send.log" "$AUD/logs/$name.recv.log" | tr '\n' ' ')
echo "$name | se=$se re=$re | $hdr | $res | $err | args: $* | t=$(echo "$end - $start" | bc)"
