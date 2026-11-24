"""First mm sender rows and first nn receiver rows of an FPSI data file (smoke tests only).
usage: python3 subset.py in.bin out.bin mm nn"""
import struct
import sys

with open(sys.argv[1], "rb") as f:
    m, n, d = struct.unpack("<QQQ", f.read(24))
    db = d // 8
    X = f.read(m * db)
    Y = f.read(n * db)
mm, nn = int(sys.argv[3]), int(sys.argv[4])
with open(sys.argv[2], "wb") as f:
    f.write(struct.pack("<QQQ", mm, nn, d))
    f.write(X[: mm * db])
    f.write(Y[: nn * db])
print(mm, nn, d)
