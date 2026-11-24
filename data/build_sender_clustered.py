"""Sender-clustered, receiver-dispersed workload (reviewers' nTU concern).
d=128, m=n=4096. Y: uniform. X: `cl` vectors within distance <= 2 of one random centre (many versions
of one file), the rest uniform; plus 512 sender vectors planted at distance 0..6 from receiver vectors
so that matches and near-misses exist. Output format as the other datasets:
u64 m, u64 n, u64 d, then m rows X and n rows Y of d/8 bytes.
usage: python3 build_sender_clustered.py <out.bin> [cl=512] [seed=7]"""
import random
import struct
import sys

out = sys.argv[1]
cl = int(sys.argv[2]) if len(sys.argv) > 2 else 512
rng = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 7)
d, m, n = 128, 4096, 4096


def rand_vec():
    return rng.getrandbits(d)


def flip(v, k):
    for i in rng.sample(range(d), k):
        v ^= 1 << i
    return v


Y = [rand_vec() for _ in range(n)]
centre = rand_vec()
X = set()
while len(X) < cl:
    X.add(flip(centre, rng.randint(0, 2)))
for j in rng.sample(range(n), 512):
    X.add(flip(Y[j], rng.randint(0, 6)))
while len(X) < m:
    X.add(rand_vec())
X = list(X)[:m]
rng.shuffle(X)
with open(out, "wb") as f:
    f.write(struct.pack("<QQQ", m, n, d))
    for v in X + Y:
        f.write(v.to_bytes(d // 8, "little"))
print(f"wrote {out}: m={m} n={n} cluster={cl}")
