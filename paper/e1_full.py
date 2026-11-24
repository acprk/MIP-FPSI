#!/usr/bin/env python3
"""Recompute E1 paired ratios stage/s4_compressed per round from all final records in runs/summary.jsonl.
usage: python3 e1_full.py experiments_r6/e1_attribution/runs/summary.jsonl"""
import json
import statistics as st
import sys
from collections import defaultdict

recs = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
fin = [r for r in recs if r.get("final")]
by = defaultdict(dict)
for r in fin:
    by[(int(r["delta"]), r["stage"])][r["round"]] = float(r["proto_cpu_s"])
for d in (4, 8):
    ref = by[(d, "s4_compressed")]
    print(f"delta={d}: s4 rounds {sorted(ref)}")
    for stage in ("s1_allpairs", "s2_full_keycheck", "s3_full_nokeycheck", "s5_compressed_Ufree"):
        rat = [v / ref[k] for k, v in by[(d, stage)].items() if k in ref]
        if rat:
            print(f"  {stage:22s} n={len(rat)} median {st.median(rat):.3f} [{min(rat):.3f}, {max(rat):.3f}]"
                  f"  abs median {st.median(by[(d, stage)].values()):.1f}")
