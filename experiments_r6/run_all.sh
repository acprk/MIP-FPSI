#!/usr/bin/env bash
# experiments_r6 driver: env record, then E2 (paired headline) and E1 (attribution), one run at a time
# on cores 40,41. Resumable (runner skips tags already finalised in summary.jsonl).
set -uo pipefail
R6=<ARTIFACT_ROOT>/experiments_r6
if ps -C fpsi_bench,fpsi_bench_kc,otfpsi,otfpsi_inproc -o pid= | read -r _; then
  echo "another benchmark is running; refusing to start" >&2; exit 1
fi
{
  echo "start=$(date) / $(TZ=America/Los_Angeles date)"
  uname -a; lscpu | grep -E "Model name|^CPU\(s\)|Thread|NUMA node[0-9]"
  cat /proc/loadavg
  cat /sys/fs/cgroup/user.slice/user-1000.slice/cpu.max
  sha256sum <ARTIFACT_ROOT>/audit_r5/build/fpsi_bench \
    <ARTIFACT_ROOT>/audit_r5/build/libmipfpsi.a \
    <ARTIFACT_ROOT>/impl/build11/libmipfpsi.a \
    $R6/harness/fpsi_bench_kc $R6/harness/fpsi_bench_ref $R6/harness/main_kc.cpp \
    <ARTIFACT_ROOT>/impl/src/main.cpp \
    $R6/otfpsi_inproc/otfpsi_inproc $R6/otfpsi_inproc/shim/coproto/Socket/AsioSocket.h \
    <ARTIFACT_ROOT>/baselines/others/xdup/01_otfpsi_cpu/build/otfpsi \
    $R6/runner.py $R6/make_jobs.py
} > $R6/env.txt 2>&1
python3 $R6/runner.py $R6/e2_paired/jobs_e2.jsonl $R6/e2_paired/runs
python3 $R6/runner.py $R6/e1_attribution/jobs_e1.jsonl $R6/e1_attribution/runs
echo "done=$(date) / $(TZ=America/Los_Angeles date)" >> $R6/env.txt
