#!/usr/bin/env python3
"""Keep the records of a runner summary.jsonl whose "end" time is not later than a cutoff.

usage: jsonl_until.py IN.jsonl OUT.jsonl "YYYY-MM-DD HH:MM:SS"

Used by reproduce_tables.sh for experiments_r6/E1: the shipped e1_attribution/tables.md and
results.csv (and the attribution numbers in the paper) were aggregated at 2026-11-12 09:10:03 PST,
while the runner kept going until 09:18:55 and appended further rounds to runs/summary.jsonl.
"""
import json
import sys

src, dst, cut = sys.argv[1], sys.argv[2], sys.argv[3]
kept = dropped = 0
with open(src) as f, open(dst, "w") as g:
    for line in f:
        if not line.strip():
            continue
        r = json.loads(line)
        if r["end"][:19] <= cut:
            g.write(line)
            kept += 1
        else:
            dropped += 1
print(f"kept {kept} records ending at or before {cut}, dropped {dropped} later records")
