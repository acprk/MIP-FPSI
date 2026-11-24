"""Inputs for the policy/fault tests (test_policies.sh).

Writes net_bench data files: header (m, n, d) as little-endian u64, then X rows, then Y rows.
clustered.bin: d=128, m=n=256; sender rows 0 and 1 differ in one bit (so U >= 2),
a few receiver rows are near sender rows (so the output is non-empty).
"""
import random
import struct
import sys

out = sys.argv[1]
rng = random.Random(20261024)
d, m, n = 128, 256, 256
db = d // 8
X = [bytearray(rng.getrandbits(8) for _ in range(db)) for _ in range(m)]
Y = [bytearray(rng.getrandbits(8) for _ in range(db)) for _ in range(n)]
X[1] = bytearray(X[0])
X[1][0] ^= 1
for j in range(16):
    Y[j] = bytearray(X[j])
    for _ in range(j % 6):
        k = rng.randrange(d)
        Y[j][k // 8] ^= 1 << (k % 8)
with open(out, "wb") as f:
    f.write(struct.pack("<QQQ", m, n, d))
    for row in X + Y:
        f.write(bytes(row))
print("wrote", out)

# empty sender / empty receiver files for the early-exit test
for name, (mm, nn) in (("empty_sender.bin", (0, n)), ("empty_receiver.bin", (m, 0))):
    path = out.rsplit("/", 1)[0] + "/" + name
    with open(path, "wb") as f:
        f.write(struct.pack("<QQQ", mm, nn, d))
        for row in X[:mm] + Y[:nn]:
            f.write(bytes(row))
    print("wrote", path)
