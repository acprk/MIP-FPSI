"""Independent brute-force |I| for every audit data file, compared with the 'truth=' and
'matches=' counts that fpsi_bench reported in logs/sweep/summary.tsv.
I = {x in X : exists y in Y, popcount(x ^ y) <= delta}, X deduplicated (set semantics).
"""
import glob
import os
import re
import struct

AUD = "<ARTIFACT_ROOT>/audit_r5"
truth = {}
for path in sorted(glob.glob(os.path.join(AUD, "data", "*.bin"))):
    stem = os.path.basename(path)[:-4]
    delta = int(re.search(r"_t(\d+)$", stem).group(1))
    with open(path, "rb") as f:
        m, n, d = struct.unpack("<QQQ", f.read(24))
        db = d // 8
        X = {int.from_bytes(f.read(db), "little") for _ in range(m)}
        Y = {int.from_bytes(f.read(db), "little") for _ in range(n)}
    truth[stem] = sum(1 for x in X if any(bin(x ^ y).count("1") <= delta for y in Y))

bad = 0
rows = 0
for line in open(os.path.join(AUD, "logs", "sweep", "summary.tsv")):
    if not line.startswith(("PASS", "FAIL")):
        continue
    parts = line.rstrip("\n").split("\t")
    stem, mode = parts[1], parts[2]
    mt = re.search(r"matches=(\d+) truth=(\d+)", line)
    if not mt:
        continue
    rows += 1
    got, tr = int(mt.group(1)), int(mt.group(2))
    if tr != truth[stem] or got != truth[stem]:
        bad += 1
        print("MISMATCH", stem, mode, "matches", got, "harness_truth", tr, "bruteforce", truth[stem])
print(f"rows_checked={rows} mismatches={bad}")
for k in sorted(truth):
    print(k, truth[k])
