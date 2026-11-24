#!/usr/bin/env python3
"""Recompute the paired otFPSI(SoftSpoken)/ours CPU ratios of E2 at full precision (no intermediate rounding).
Ratio per seed = otfpsi_softspoken proto_cpu_s (one run per seed) / median of our 3 final reps' proto_cpu_s.
usage: python3 paired_exact.py experiments_r6/e2_paired/results.csv"""
import csv
import statistics
import sys
from decimal import Decimal, ROUND_HALF_UP

rows = [r for r in csv.DictReader(open(sys.argv[1])) if r["final"] == "True"]
ot = {}
ours = {}
for r in rows:
    if r["kind"] == "ours":
        ours.setdefault((int(r["delta"]), r["mode"], int(r["seed"])), []).append(float(r["proto_cpu_s"]))
    elif r["mode"] == "otfpsi_softspoken" or "softspoken" in r["mode"] or "softspoken" in r["kind"]:
        ot[int(r["seed"])] = float(r["proto_cpu_s"])
print("otfpsi softspoken per seed:", ot)


def ri(x):
    return int(Decimal(repr(x)).quantize(Decimal("1"), rounding=ROUND_HALF_UP))


for delta in (4, 8):
    for mode in ("exact", "pow2", "Ufree"):
        rat = []
        for s in range(1, 6):
            v = ours[(delta, mode, s)]
            rat.append(ot[s] / statistics.median(v))
        med = statistics.median(rat)
        print(f"delta={delta} {mode:6s} per-seed " + " ".join(f"{x:.3f}" for x in rat)
              + f" | median {med:.3f} [{min(rat):.3f}, {max(rat):.3f}]"
              + f" | int: {[ri(x) for x in rat]} median {ri(med)} [{ri(min(rat))}, {ri(max(rat))}]")
