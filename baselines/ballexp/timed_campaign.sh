#!/bin/bash
# Timed runs of the ball-expansion baseline (synthetic items of the right sizes).
# Gate: campaign2 "followup4 done=" line, then per run: 1-min load < 10 and enough MemAvailable.
# Records load and delta nr_throttled of user-1000.slice per run in logs/<label>.env.
set -u
DIR=<ARTIFACT_ROOT>/baselines/ballexp
F=<ARTIFACT_ROOT>/results/campaign2_20261023_1344/env.txt
CS=/sys/fs/cgroup/user.slice/user-1000.slice/cpu.stat
# (followup4 gate dropped by coordinator: host stays loaded; primary metric = CPU seconds)
echo "start $(date); campaign2 env tail: $(tail -1 $F)"

run() {
  label=$1; needGB=$2; shift 2
  while true; do
    l1=$(cut -d' ' -f1 /proc/loadavg)
    memkb=$(awk '/MemAvailable/{print $2}' /proc/meminfo)
    if awk -v l=$l1 'BEGIN{exit !(l<30)}' && [ $memkb -gt $((needGB*1024*1024)) ]; then break; fi
    sleep 60
  done
  thr0=$(awk '/^nr_throttled/{print $2}' $CS); tus0=$(awk '/^throttled_usec/{print $2}' $CS)
  {
    echo "label=$label start=$(date) loadavg_before=$(cat /proc/loadavg) MemAvailable_kB=$memkb"
    echo "threads on cores 40,41,92,93 (running) before:"
    ps -eLo pid,psr,stat,comm | awk '($2==40||$2==41||$2==92||$2==93) && $3 ~ /R/'
  } > $DIR/logs/$label.env
  bash $DIR/run_pair.sh $label 40 41 1421 "$@"
  thr1=$(awk '/^nr_throttled/{print $2}' $CS); tus1=$(awk '/^throttled_usec/{print $2}' $CS)
  echo "end=$(date) loadavg_after=$(cat /proc/loadavg) d_nr_throttled=$((thr1-thr0)) d_throttled_usec=$((tus1-tus0))" >> $DIR/logs/$label.env
  cat $DIR/logs/$label.env
}

run synth_256_d64t3 10 -mode synth -m 256 -n 256 -d 64 -delta 3 -seed 3
run synth_cpython_m2973_n2959_d64t3 42 -mode synth -m 2973 -n 2959 -d 64 -delta 3 -seed 3
run synth_2e12_d64t3 55 -mode synth -m 4096 -n 4096 -d 64 -delta 3 -seed 3
echo "campaign done $(date)"
