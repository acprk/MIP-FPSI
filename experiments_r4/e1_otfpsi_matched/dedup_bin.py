"""Deduplicate the rows of an FPSI data file (both sets), keeping first occurrences, so that a
pairwise baseline runs on the same sets as our protocol (which deduplicates locally).
usage: python3 dedup_bin.py in.bin out.bin"""
import struct
import sys

with open(sys.argv[1], "rb") as f:
    m, n, d = struct.unpack("<QQQ", f.read(24))
    db = d // 8
    X = [f.read(db) for _ in range(m)]
    Y = [f.read(db) for _ in range(n)]
X = list(dict.fromkeys(X))
Y = list(dict.fromkeys(Y))
with open(sys.argv[2], "wb") as f:
    f.write(struct.pack("<QQQ", len(X), len(Y), d))
    for r in X + Y:
        f.write(r)
print(len(X), len(Y))
