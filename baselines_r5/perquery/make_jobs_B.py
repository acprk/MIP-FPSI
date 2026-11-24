#!/usr/bin/env python3
"""Write jobs_B.jsonl: per-query comparison, Bui-Cong flash-psi (one query vs N) vs ours (m=N, n=1)."""
import json

SIM = "<ARTIFACT_ROOT>/baselines_r5/perquery/flash-psi/target/release/simulation"
FB = "<ARTIFACT_ROOT>/baselines_r5/gao_ext/build_ours/fpsi_bench"
OUT = "<ARTIFACT_ROOT>/baselines_r5/perquery/jobs_B.jsonl"


def flash(tag, n, extra):
    return {"tag": tag, "kind": "generic",
            "cmd": [SIM, "-m", str(n), "--weight", "14", "--subsample-count", "64", "-t", "2", "--track-io", *extra]}


def ours(tag, n, extra):
    return {"tag": tag, "kind": "ours",
            "cmd": [FB, "-m", str(n), "-n", "1", "-d", "128", "-delta", "4", "-b", "5", "-s", "1", "-nt", "1",
                    "-plant", "1", "-seed", "2", "-indexverify", *extra]}


jobs = []
for k in (1, 2, 3):
    for n, lab in ((4096, "2e12"), (10000, "1e4"), (65536, "2e16")):
        jobs.append(flash(f"flash_shipped_{lab}_r{k}", n, []))
        jobs.append(flash(f"flash_full_{lab}_r{k}", n, ["--full-scan"]))
        if lab != "1e4":
            jobs.append(flash(f"flash_padfull_{lab}_r{k}", n, ["--full-scan", "--padding"]))
            jobs.append(ours(f"ours_q_{lab}_r{k}", n, []))
            jobs.append(ours(f"ours_q_nofb_{lab}_r{k}", n, ["-nofallback"]))
with open(OUT, "w") as f:
    for j in jobs:
        f.write(json.dumps(j) + "\n")
print(len(jobs), "jobs ->", OUT)
