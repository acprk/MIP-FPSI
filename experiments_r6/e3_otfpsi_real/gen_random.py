"""Uniformly random FPSI data file with the same m, n, d as a reference file (rows distinct within
each set, like the deduplicated real sets). Used to test whether otFPSI's cost depends on content.
usage: python3 gen_random.py ref.bin out.bin seed"""
import random
import struct
import sys

with open(sys.argv[1], "rb") as f:
    m, n, d = struct.unpack("<QQQ", f.read(24))
rng = random.Random(int(sys.argv[3]))


def rows(cnt):
    s = set()
    while len(s) < cnt:
        s.add(rng.getrandbits(d).to_bytes(d // 8, "little"))
    out = list(s)
    rng.shuffle(out)
    return out


with open(sys.argv[2], "wb") as f:
    f.write(struct.pack("<QQQ", m, n, d))
    for r in rows(m) + rows(n):
        f.write(r)
print(m, n, d)
