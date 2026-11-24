#!/usr/bin/env python3
"""Write jobs_A.jsonl for runner.py: Gao (original + bit-level-patched copy) vs ours (exact, pow2),
interleaved per repetition so that every ratio is same-host and paired in time."""
import json
import zlib

GAO = "<ARTIFACT_ROOT>/../code_repro/Fuzzy-Private-Set-Intersection-from-Fuzzy-Mapping/build/main"
GAOP = "<ARTIFACT_ROOT>/baselines_r5/gao_ext/patched/build/main"
FB = "<ARTIFACT_ROOT>/baselines_r5/gao_ext/build_ours/fpsi_bench"
OUT = "<ARTIFACT_ROOT>/baselines_r5/gao_ext/jobs_A.jsonl"


def gao(tag, binary, lg, delta, extra=()):
    return {"tag": tag, "kind": "gao", "cmd": [binary, "-fpsi", "-t13", "-hams", str(lg), "-hamr", str(lg),
                                               "-hamdelta", str(delta), "-hamd", "128", "-hami", "6", *extra]}


def ours(tag, lg, delta, mode):
    n = 1 << lg
    b = delta + 1
    cmd = [FB, "-m", str(n), "-n", str(n), "-d", "128", "-delta", str(delta), "-b", str(b), "-s", "1",
           "-nt", "1", "-indexverify", "-seed", tag_seed(tag)]
    if mode == "pow2":
        cmd.append("-pow2")
    return {"tag": tag, "kind": "ours", "cmd": cmd}


def tag_seed(tag):
    return str(zlib.crc32(tag.encode()) % 100000 + 1)


jobs = []
for k in (1, 2, 3):
    for lg in (8, 10, 12):
        jobs.append(gao(f"gao_d4_2e{lg}_r{k}", GAO, lg, 4))
        jobs.append(ours(f"ours_exact_d4_2e{lg}_r{k}", lg, 4, "exact"))
        jobs.append(ours(f"ours_pow2_d4_2e{lg}_r{k}", lg, 4, "pow2"))
    if k == 1:  # build control: patched copy without -hambits must behave like the original
        jobs.append(gao("gaoPctrl_d4_2e8_r1", GAOP, 8, 4))
    jobs.append(gao(f"gao_d8_2e7_r{k}", GAO, 7, 8))
    jobs.append(ours(f"ours_exact_d8_2e7_r{k}", 7, 8, "exact"))
    jobs.append(ours(f"ours_pow2_d8_2e7_r{k}", 7, 8, "pow2"))
    for lg in (8, 10, 12):
        j = gao(f"gaoP_d8_2e{lg}_r{k}", GAOP, lg, 8, ("-hambits",))
        if lg == 12:
            j["skip_if_total_over"] = ["gaoP_d8_2e10_r1", 3600, 4.0]
        jobs.append(j)
        jobs.append(ours(f"ours_exact_d8_2e{lg}_r{k}", lg, 8, "exact"))
        jobs.append(ours(f"ours_pow2_d8_2e{lg}_r{k}", lg, 8, "pow2"))
with open(OUT, "w") as f:
    for j in jobs:
        f.write(json.dumps(j) + "\n")
print(len(jobs), "jobs ->", OUT)
