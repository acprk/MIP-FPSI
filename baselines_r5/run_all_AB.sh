#!/usr/bin/env bash
# Resume campaign A (skips tags already in runs/summary.jsonl), then run the per-query campaign B.
# Each job runs alone, pinned to cores 42,43; the runner itself sits on core 70.
R=<ARTIFACT_ROOT>/baselines_r5
taskset -c 70 python3 $R/gao_ext/runner.py $R/gao_ext/jobs_A.jsonl $R/gao_ext/runs >> $R/gao_ext/runner_A.out 2>&1
taskset -c 70 python3 $R/gao_ext/runner.py $R/perquery/jobs_B.jsonl $R/perquery/runs >> $R/perquery/runner_B.out 2>&1
echo "all done $(date)" >> $R/gao_ext/runner_A.out
