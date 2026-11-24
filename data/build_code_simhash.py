"""Code near-duplicate dataset: 64-bit SimHash of source files of two CPython releases.

X = files of release A (sender), Y = files of release B (receiver). Same-path files that changed
little between releases are natural near-duplicates. Writes fpsi_bench input + stats.

SimHash (Charikar 2002; Manku et al. WWW'07): features = 3-shingles of tokens, weight = count,
64-bit blake2b per feature, bitwise signed sum, sign.
"""
import hashlib
import json
import re
import struct
import sys
import tarfile
from collections import Counter

import numpy as np

EXT = (".py", ".c", ".h", ".rst")
TOK = re.compile(r"[A-Za-z_][A-Za-z0-9_]*|\d+|\S")


def simhash64(text):
    toks = TOK.findall(text)
    feats = Counter(" ".join(toks[i:i + 3]) for i in range(max(1, len(toks) - 2)))
    acc = np.zeros(64, dtype=np.int64)
    for f, w in feats.items():
        h = int.from_bytes(hashlib.blake2b(f.encode(), digest_size=8).digest(), "little")
        bits = (h >> np.arange(64, dtype=np.uint64)) & np.uint64(1)
        acc += np.where(bits == 1, w, -w)
    return (acc > 0).astype(np.uint8)


def load(tgz):
    out = {}
    with tarfile.open(tgz) as t:
        for mem in t.getmembers():
            if not mem.isfile() or not mem.name.endswith(EXT):
                continue
            rel = mem.name.split("/", 1)[1]
            data = t.extractfile(mem).read().decode("utf-8", "replace")
            if len(data) < 200:  # tiny files collide trivially; skip
                continue
            out[rel] = data
    return out


def pack(rows):
    rows = np.asarray(rows, dtype=np.uint8)
    return np.packbits(rows, axis=1, bitorder="little")


if __name__ == "__main__":
    a, b, outp = sys.argv[1], sys.argv[2], sys.argv[3]
    A, Bf = load(a), load(b)
    ka, kb = sorted(A), sorted(Bf)
    X = [simhash64(A[k]) for k in ka]
    Y = [simhash64(Bf[k]) for k in kb]
    Xp, Yp = pack(X), pack(Y)
    with open(outp, "wb") as f:
        f.write(struct.pack("<QQQ", len(Xp), len(Yp), 64))
        f.write(Xp.tobytes())
        f.write(Yp.tobytes())
    Xa, Ya = np.array(X), np.array(Y)
    common = sorted(set(ka) & set(kb))
    same = [int((Xa[ka.index(k)] != Ya[kb.index(k)]).sum()) for k in common]
    rng = np.random.default_rng(0)
    i = rng.integers(0, len(Xa), 20000)
    j = rng.integers(0, len(Ya), 20000)
    diff = (Xa[i] != Ya[j]).sum(1)
    hist = Counter(same)
    stats = dict(m=len(Xp), n=len(Yp), common_paths=len(common),
                 same_path_dist_hist={int(k): v for k, v in sorted(hist.items()) if k <= 16},
                 same_path_le3=sum(v for k, v in hist.items() if k <= 3),
                 random_pair_dist_mean=float(diff.mean()), random_pair_le3=int((diff <= 3).sum()))
    json.dump(stats, open(outp + ".json", "w"), indent=1)
    print(json.dumps(stats, indent=1))
