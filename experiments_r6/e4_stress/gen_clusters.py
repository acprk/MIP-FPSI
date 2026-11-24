"""E4(a) inputs: receiver clusters of controlled size k on table 0 of the public (5,1) block cover.

d=128, delta=4, n=m=2^12. Receiver: k vectors share one random value on the bit positions of
table 0 (all other bits uniform); the remaining n-k vectors are uniform. Rejection sampling keeps
every other bucket of every table at load 1, so the maximum bucket load is exactly B = k
(k = 1 gives a collision-free, "uniform-ish" receiver set).
Sender: m uniform vectors, 512 of them replaced by near-copies (0..delta+2 random bit flips) of
distinct receiver vectors; min(k, 8) of the copied receiver vectors are cluster members, so the
cluster contributes true matches without making the sender's probe multiplicity U large.
File format as read by fpsi_bench -data: u64 m, n, d (LE), then m rows X, n rows Y, d/8 bytes each.
Usage: python3 gen_clusters.py <cover_dump.txt> <outdir>
"""
import os
import random
import struct
import sys

cover_file, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
lines = open(cover_file).read().split("\n")
tables = [[int(v) for v in ln.split()] for ln in lines[1:] if ln.strip()]
d, delta, n, m, PLANT = 128, 4, 4096, 4096, 512


def key(v, t):
    return tuple((v >> p) & 1 for p in tables[t])


def flip(v, w, rng):
    for p in rng.sample(range(d), w):
        v ^= 1 << p
    return v


def to_bytes(v):
    return v.to_bytes(d // 8, "little")  # bit k = byte k//8, bit k%8


for k in (1, 2, 4, 8, 16, 32, 64, 128, 512, 2048):
    rng = random.Random(7000 + k)
    seen = [dict() for _ in tables]  # key -> count, per table
    Y = []
    centre = rng.getrandbits(d)
    ckey = key(centre, 0)

    def admissible(v, in_cluster):
        for t in range(len(tables)):
            kk = key(v, t)
            if t == 0 and in_cluster:
                continue
            if kk in seen[t] or (t == 0 and kk == ckey):
                return False
        return True

    def add(v):
        for t in range(len(tables)):
            kk = key(v, t)
            seen[t][kk] = seen[t].get(kk, 0) + 1
        Y.append(v)

    mask0 = 0
    for p in tables[0]:
        mask0 |= 1 << p
    while len(Y) < k:
        v = (rng.getrandbits(d) & ~mask0) | (centre & mask0)
        if admissible(v, True):
            add(v)
    while len(Y) < n:
        v = rng.getrandbits(d)
        if admissible(v, False):
            add(v)
    loads = [max(s.values()) for s in seen]
    assert max(loads) == k, (k, loads)

    X = [rng.getrandbits(d) for _ in range(m)]
    ncl = min(k, 8)
    src = list(range(ncl)) + rng.sample(range(k, n), PLANT - ncl)
    pos = rng.sample(range(m), PLANT)
    for i, j in zip(pos, src):
        X[i] = flip(Y[j], rng.randrange(delta + 3), rng)
    rng.shuffle(Y)
    path = os.path.join(out, f"cluster_k{k}.bin")
    with open(path, "wb") as f:
        f.write(struct.pack("<QQQ", m, n, d))
        for v in X + Y:
            f.write(to_bytes(v))
    print(path, "B_per_table", loads)
