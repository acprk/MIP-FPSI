#!/bin/bash
# Usage: run_pair.sh <label> <coreS> <coreR> <port> [driver args...]
# Runs sender (role 0) and receiver (role 1) as two processes over loopback TCP, each pinned with
# taskset, wrapped in /usr/bin/time -v for peak RSS. Logs go to logs/<label>.{S,R}.log
set -u
DIR=<ARTIFACT_ROOT>/baselines/ballexp
BIN=$DIR/build/ballexp_bench
label=$1; cS=$2; cR=$3; port=$4; shift 4
mkdir -p $DIR/logs
echo "cmd: $BIN -addr 127.0.0.1:$port $* (S core $cS, R core $cR)" > $DIR/logs/$label.cmd
echo "start $(date)" >> $DIR/logs/$label.cmd
taskset -c $cS /usr/bin/time -v $BIN -role 0 -addr 127.0.0.1:$port "$@" > $DIR/logs/$label.S.log 2>&1 &
spid=$!
taskset -c $cR /usr/bin/time -v $BIN -role 1 -addr 127.0.0.1:$port "$@" > $DIR/logs/$label.R.log 2>&1
rr=$?
wait $spid
rs=$?
echo "end $(date) exitS=$rs exitR=$rr" >> $DIR/logs/$label.cmd
echo "== $label exitS=$rs exitR=$rr"
grep -E "^(role|prep|psi|intersection|real_check|planted)|Maximum resident|Elapsed" $DIR/logs/$label.S.log $DIR/logs/$label.R.log
