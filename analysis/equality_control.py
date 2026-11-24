"""Application control: how many fuzzy outputs would exact equality already find?
usage: python3 equality_control.py <data.bin> <delta>   (format: u64 m, u64 n, u64 d, X rows, Y rows)"""
import struct
import sys

path, delta = sys.argv[1], int(sys.argv[2])
with open(path, "rb") as f:
    m, n, d = struct.unpack("<QQQ", f.read(24))
    db = d // 8
    X = {int.from_bytes(f.read(db), "little") for _ in range(m)}
    Y = {int.from_bytes(f.read(db), "little") for _ in range(n)}
exact = sum(1 for x in X if x in Y)
# fuzzy outputs via block pigeonhole index (exact computation)
b = delta + 1
blk = [(i * d // b, (i + 1) * d // b) for i in range(b)]
index = [dict() for _ in range(b)]
for y in Y:
    for i, (lo, hi) in enumerate(blk):
        index[i].setdefault((y >> lo) & ((1 << (hi - lo)) - 1), []).append(y)
fuzzy = 0
for x in X:
    hit = False
    for i, (lo, hi) in enumerate(blk):
        for y in index[i].get((x >> lo) & ((1 << (hi - lo)) - 1), []):
            if bin(x ^ y).count("1") <= delta:
                hit = True
                break
        if hit:
            break
    fuzzy += hit
print(f"{path}: |X|={len(X)} |Y|={len(Y)} exact-equality={exact} fuzzy(delta={delta})={fuzzy} extra={fuzzy - exact}")
