#!/usr/bin/env bash
# Run one two-process net_bench instance (receiver on core RC, sender on core SC).
# Usage: ours_pair.sh OUTPREFIX SC RC PORT net_bench-args...
# Writes OUTPREFIX.{send,recv}.{log,time}; exit status = receiver status (2 = FN/FP found).
set -u
NB=<ARTIFACT_ROOT>/impl/build11/net_bench
pre=$1 sc=$2 rc=$3 port=$4; shift 4
/usr/bin/time -v -o "$pre.recv.time" taskset -c "$rc" "$NB" -role 1 -addr 127.0.0.1:$port -indexverify "$@" > "$pre.recv.log" 2>&1 &
rp=$!
sleep 0.3
/usr/bin/time -v -o "$pre.send.time" taskset -c "$sc" "$NB" -role 0 -addr 127.0.0.1:$port "$@" > "$pre.send.log" 2>&1
se=$?
wait $rp
re=$?
echo "sender_exit=$se receiver_exit=$re"
exit $re
