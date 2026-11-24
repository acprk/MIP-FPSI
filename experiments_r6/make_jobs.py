#!/usr/bin/env python3
"""Write the job lists for E1 (attribution) and E2 (paired headline). Shuffles use a fixed RNG
seed so the run order is reproducible; the order is also visible in jobs_*.jsonl."""
import json, random

R6 = "<ARTIFACT_ROOT>/experiments_r6"
FB = "<ARTIFACT_ROOT>/audit_r5/build/fpsi_bench"
KC = R6 + "/harness/fpsi_bench_kc"
OT = R6 + "/otfpsi_inproc/otfpsi_inproc"
rng = random.Random(20261112)


def base(delta, seed):
    return ["-m", "4096", "-n", "4096", "-d", "128", "-delta", str(delta), "-b", str(delta + 1), "-s", "1",
            "-nt", "1", "-indexverify", "-seed", str(seed)]


def otf(mode):
    return [OT, "-proto_distance", "batched", "-proto_threshold", "np", "-querier_size", "4096",
            "-responder_size", "4096", "-vector_size", "128", "-threshold", "4", "-prf", "aes", "-ot", mode]


# ---- E1: same data (seed 1), 5 rounds, random order within each round over both deltas ----
E1_STAGES = {
    "s1_allpairs": (FB, ["-allpairs"]),
    "s2_full_keycheck": (KC, ["-nocompress", "-keycheck"]),
    "s3_full_nokeycheck": (FB, ["-nocompress"]),
    "s4_compressed": (FB, []),
    "s4kc_harnesscheck": (KC, []),
    "s5_compressed_Ufree": (FB, ["-noU"]),
}
with open(R6 + "/e1_attribution/jobs_e1.jsonl", "w") as f:
    for k in range(1, 6):
        js = []
        for delta in (4, 8):
            for st, (binp, extra) in E1_STAGES.items():
                js.append({"tag": f"e1_d{delta}_{st}_r{k}", "kind": "ours", "exp": "E1", "delta": delta,
                           "stage": st, "round": k, "seed": 1, "cmd": [binp] + base(delta, 1) + extra})
        rng.shuffle(js)
        for j in js:
            f.write(json.dumps(j) + "\n")

# ---- E2: 5 data seeds; per seed one block = native otFPSI (softspoken + silent) and
#      ours {exact, rounded, U-free} x 3 reps x delta {4,8}, random order within the block ----
MODES = {"exact": [], "pow2": ["-pow2"], "Ufree": ["-pow2", "-noU"]}
with open(R6 + "/e2_paired/jobs_e2.jsonl", "w") as f:
    for seed in range(1, 6):
        js = [{"tag": f"e2_s{seed}_ot_softspoken", "kind": "otfpsi", "exp": "E2", "seed": seed,
               "mode": "otfpsi_softspoken", "cmd": otf("softspoken")},
              {"tag": f"e2_s{seed}_ot_silent", "kind": "otfpsi", "exp": "E2", "seed": seed,
               "mode": "otfpsi_silent", "cmd": otf("silent")}]
        for delta in (4, 8):
            for mode, extra in MODES.items():
                for rep in range(1, 4):
                    js.append({"tag": f"e2_s{seed}_d{delta}_{mode}_r{rep}", "kind": "ours", "exp": "E2",
                               "seed": seed, "delta": delta, "mode": mode, "rep": rep,
                               "cmd": [FB] + base(delta, seed) + extra})
        rng.shuffle(js)
        for j in js:
            f.write(json.dumps(j) + "\n")
