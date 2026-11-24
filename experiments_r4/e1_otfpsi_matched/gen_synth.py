"""Synthetic FPSI input like net_bench: uniform X, Y with m/8 planted near-copies of sender vectors
(0..delta+2 random flips). Output: u64 m, n, d; X rows; Y rows (bit k = bit k%8 of byte k/8).
usage: python3 gen_synth.py m n d delta seed out.bin"""
import random
import struct
import sys

m, n, d, delta, seed, out = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6]
rng = random.Random(seed)
db = d // 8
X = [bytearray(rng.getrandbits(8) for _ in range(db)) for _ in range(m)]
Y = [bytearray(rng.getrandbits(8) for _ in range(db)) for _ in range(n)]
for _ in range(min(m, n) // 8):
    i, j = rng.randrange(m), rng.randrange(n)
    Y[j] = bytearray(X[i])
    for _ in range(rng.randrange(delta + 3)):
        k = rng.randrange(d)
        Y[j][k // 8] ^= 1 << (k % 8)
with open(out, "wb") as f:
    f.write(struct.pack("<QQQ", m, n, d))
    for r in X + Y:
        f.write(bytes(r))
