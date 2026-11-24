#!/usr/bin/env bash
# Wait until campaign A's runner (pid $1) has exited, then run the per-query jobs (one at a time, cores 42,43).
while [ -e "/proc/$1" ]; do sleep 30; done
exec taskset -c 70 python3 <ARTIFACT_ROOT>/baselines_r5/gao_ext/runner.py \
  <ARTIFACT_ROOT>/baselines_r5/perquery/jobs_B.jsonl \
  <ARTIFACT_ROOT>/baselines_r5/perquery/runs
