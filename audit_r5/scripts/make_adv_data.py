"""Adversarial inputs for the audit_r5 correctness sweep.

File format (as read by fpsi_bench / net_bench -data): u64 m, n, d (little endian), then m rows
of X, then n rows of Y, d/8 bytes per row, bit k = byte k//8, bit k%8.
Usage: python3 make_adv_data.py <outdir>
"""
import os
import random
import struct
import sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def rnd(rng, db):
    return bytearray(rng.getrandbits(8) for _ in range(db))


def flip(v, d, k, rng, avoid=None):
    v = bytearray(v)
    pos = rng.sample(range(d), k) if avoid is None else rng.sample([p for p in range(d) if p not in avoid], k)
    for p in pos:
        v[p // 8] ^= 1 << (p % 8)
    return v


def write(name, d, X, Y):
    with open(os.path.join(out, name + ".bin"), "wb") as f:
        f.write(struct.pack("<QQQ", len(X), len(Y), d))
        for r in X + Y:
            f.write(bytes(r))


def cluster(rng, d, count, free_bits):
    """count vectors equal to one centre except on free_bits fixed low positions."""
    db = d // 8
    c = rnd(rng, db)
    free = rng.sample(range(d), free_bits)
    res = []
    seen = set()
    while len(res) < count:
        v = bytearray(c)
        for p in free:
            if rng.getrandbits(1):
                v[p // 8] ^= 1 << (p % 8)
        if bytes(v) not in seen:
            seen.add(bytes(v))
            res.append(v)
    return res, free


for d in (64, 128):
    db = d // 8
    for delta in (3, 4, 8):
        rng = random.Random(1000 * d + delta)
        tag = f"d{d}_t{delta}"
        # 1. boundary pairs: exactly delta and delta+1 (and 0) apart
        m = n = 256
        X = [rnd(rng, db) for _ in range(m)]
        Y = [rnd(rng, db) for _ in range(n)]
        for k in range(64):
            Y[k] = flip(X[k], d, delta, rng)
        for k in range(64, 128):
            Y[k] = flip(X[k], d, delta + 1, rng)
        for k in range(128, 140):
            Y[k] = bytearray(X[k])
        write(f"boundary_{tag}", d, X, Y)

        # 2. clustered receiver (large B): 200 receiver vectors differing only on 8 positions
        Yc, free = cluster(rng, d, 200, 8)
        Y = Yc + [rnd(rng, db) for _ in range(56)]
        X = [rnd(rng, db) for _ in range(256)]
        for k in range(40):
            X[k] = flip(Yc[k], d, delta, rng, avoid=set(free))
        for k in range(40, 80):
            X[k] = flip(Yc[k], d, delta + 1, rng, avoid=set(free))
        write(f"clusterR_{tag}", d, X, Y)

        # 3. clustered sender (large U)
        Xc, free = cluster(rng, d, 200, 8)
        X = Xc + [rnd(rng, db) for _ in range(56)]
        Y = [rnd(rng, db) for _ in range(256)]
        for k in range(40):
            Y[k] = flip(Xc[k], d, delta, rng, avoid=set(free))
        for k in range(40, 80):
            Y[k] = flip(Xc[k], d, delta + 1, rng, avoid=set(free))
        write(f"clusterS_{tag}", d, X, Y)

        # 4. both clustered around the same centre region
        Xc, free = cluster(rng, d, 120, 7)
        Y = [flip(v, d, rng.randrange(delta + 2), rng) for v in Xc[:100]] + [rnd(rng, db) for _ in range(28)]
        X = Xc + [rnd(rng, db) for _ in range(8)]
        write(f"clusterRS_{tag}", d, X, Y)

        # 5. duplicates on both sides (exact duplicates, plus exact matches)
        base = [rnd(rng, db) for _ in range(100)]
        X = base + base[:60] + base[:30]
        Yb = [flip(v, d, rng.randrange(delta + 2), rng) for v in base[:50]] + [rnd(rng, db) for _ in range(50)]
        Y = Yb + Yb[:40] + [bytearray(v) for v in base[90:100]]
        rng.shuffle(X)
        rng.shuffle(Y)
        write(f"dups_{tag}", d, X, Y)

        # 6. m != n
        X = [rnd(rng, db) for _ in range(37)]
        Y = [rnd(rng, db) for _ in range(700)]
        for k in range(20):
            Y[k] = flip(X[k], d, delta if k % 2 else delta + 1, rng)
        write(f"mlt_{tag}", d, X, Y)
        X = [rnd(rng, db) for _ in range(700)]
        Y = [rnd(rng, db) for _ in range(9)]
        for k in range(9):
            X[k] = flip(Y[k], d, delta if k % 2 else delta + 1, rng)
        write(f"mgt_{tag}", d, X, Y)
        X = [rnd(rng, db) for _ in range(1024)]
        Y = [rnd(rng, db) for _ in range(300)]
        for k in range(150):
            Y[k] = flip(X[3 * k], d, delta - (k % 3) if k % 2 else delta + 1, rng)
        write(f"big_{tag}", d, X, Y)

        # 7. singletons and empty sets
        x = rnd(rng, db)
        write(f"one_eq_{tag}", d, [x], [flip(x, d, delta, rng)])
        write(f"one_ne_{tag}", d, [x], [flip(x, d, delta + 1, rng)])
        write(f"emptyX_{tag}", d, [], [rnd(rng, db) for _ in range(10)])
        write(f"emptyY_{tag}", d, [rnd(rng, db) for _ in range(10)], [])
        write(f"emptyXY_{tag}", d, [], [])
print("ok", out)
