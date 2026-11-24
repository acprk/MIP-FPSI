"""Print m n of an FPSI data file header (u64 m, u64 n, u64 d)."""
import struct
import sys

m, n, d = struct.unpack("<QQQ", open(sys.argv[1], "rb").read(24))
print(m, n)
