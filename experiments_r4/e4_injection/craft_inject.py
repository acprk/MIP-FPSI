"""E4: content-level injection against the load leakage on the CPython workload.

Threat: the vendor (sender) can get files admitted into the auditor's corpus (the receiver's Y) and
wants to learn whether the auditor holds a file whose SimHash has key z in table t (projection
occupancy). It writes k ordinary-looking Python files whose SimHash (computed by the same pipeline as
the paper's dataset, build_code_simhash.simhash64) has key z in table t, by greedily appending
identifier tokens to a comment block. The crafted files are written to disk and fingerprinted again
from disk, so the whole path content -> fingerprint -> deduplication -> protocol is exercised.

For each target (a receiver file alone in its bucket) we build two corpora of equal deduplicated
size: H1 = Y (contains the target) + injected files, H0 = Y with the target replaced by a held-out
real file that avoids the bucket + injected files. Writes data files for net_bench and a manifest.
usage: python3 craft_inject.py <Python-3.11.9.tgz> <Python-3.12.4.tgz> <cover_dump.txt> <outdir> [targets] [k]
"""
import hashlib
import json
import os
import random
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../../data"))
from build_code_simhash import TOK, load, simhash64  # noqa: E402

a_tgz, b_tgz, cover_txt, out = sys.argv[1:5]
NT = int(sys.argv[5]) if len(sys.argv) > 5 else 10
K = int(sys.argv[6]) if len(sys.argv) > 6 else 64
os.makedirs(out, exist_ok=True)
rng = random.Random(2026)

tables = [list(map(int, l.split())) for l in open(cover_txt) if l.strip() and not l.startswith("r ")]
A, Bf = load(a_tgz), load(b_tgz)
X = [simhash64(A[k]) for k in sorted(A)]
Yfiles = sorted(Bf)
Yfp = {}
for k in Yfiles:
    Yfp.setdefault(bytes(simhash64(Bf[k])), k)  # deduplicate fingerprints, keep one path


def key(fp, t):
    return tuple(int(fp[i]) for i in tables[t])


def loads(fps):
    c = {}
    for fp in fps:
        for t in range(len(tables)):
            c[(t, key(fp, t))] = c.get((t, key(fp, t)), 0) + 1
    return c


Ylist = [np.frombuffer(b, dtype=np.uint8) for b in Yfp]
L = loads(Ylist)
C = max(L.values())
print("receiver fingerprints", len(Ylist), "max load C", C, flush=True)
if K <= C:
    sys.exit("k must exceed the largest competing load")

# held-out replacements: real 3.11.9-only files whose fingerprints are not in Y
holdout = [np.array(simhash64(A[k]), dtype=np.uint8) for k in sorted(set(A) - set(Bf))]
holdout = [h for h in holdout if bytes(h) not in Yfp]

WORDS = [w for w in set(TOK.findall(" ".join(list(Bf.values())[:200]))) if w.isidentifier() and len(w) > 2]


def fhash(feat):
    h = int.from_bytes(hashlib.blake2b(feat.encode(), digest_size=8).digest(), "little")
    return np.array([(h >> b) & 1 for b in range(64)], dtype=np.int64) * 2 - 1  # +1 / -1 per bit


def acc_of(text):
    from collections import Counter
    toks = TOK.findall(text)
    feats = Counter(" ".join(toks[i:i + 3]) for i in range(max(1, len(toks) - 2)))
    acc = np.zeros(64, dtype=np.int64)
    for f, w in feats.items():
        acc += w * fhash(f)
    return acc, feats, toks


def craft(t, z, seed):
    """Python file whose SimHash has key z in table t; other tables random.
    Greedy and incremental: appending one identifier adds exactly one new 3-token shingle, whose
    +-1 contribution per SimHash bit is known, so each step picks the candidate identifier that
    moves the most wrong target bits towards their desired value. The result is re-fingerprinted
    from scratch with the dataset's simhash64 before use."""
    r = random.Random(seed)
    head = (f'"""Utility helpers ({r.randrange(10**6)}).\n\nInternal module; generated documentation follows."""\n\n'
            f"def helper_{r.randrange(10**6)}(value):\n    return value\n\n# notes:")
    acc, feats, toks = acc_of(head)
    pos = tables[t]
    want = np.array([1 if b else -1 for b in z], dtype=np.int64)
    added = []
    for _ in range(3000):
        cur = acc[pos]
        ok = (cur > 0) == (want > 0)
        text = head + " " + " ".join(added) + "\n"
        if ok.all() and len(text) >= 200:
            fp = simhash64(text)
            if key(fp, t) == z:
                return text, fp
        best, best_score, best_v, best_f = None, None, None, None
        for w in r.sample(WORDS, 64):
            f = " ".join([toks[-2], toks[-1], w])
            v = fhash(f) * (1 if feats.get(f, 0) >= 0 else 1)
            gain = v[pos] * want  # +1 if it pushes towards the desired sign
            score = 10 * gain[~ok].sum() + gain[ok & (np.abs(cur) <= 1)].sum()
            if best_score is None or score > best_score:
                best, best_score, best_v, best_f = w, score, v, f
        acc += best_v
        feats[best_f] = feats.get(best_f, 0) + 1
        toks.append(best)
        added.append(best)
    raise RuntimeError("craft failed")


def write_bin(path, Xs, Ys):
    with open(path, "wb") as f:
        f.write(struct.pack("<QQQ", len(Xs), len(Ys), 64))
        for r in list(Xs) + list(Ys):
            f.write(np.packbits(np.asarray(r, dtype=np.uint8), bitorder="little").tobytes())


singles = [(i, t) for i, fp in enumerate(Ylist) for t in range(len(tables)) if L[(t, key(fp, t))] == 1]
rng.shuffle(singles)
manifest = dict(C=C, k=K, receiver_size=len(Ylist), targets=[])
used = set()
for (i, t) in singles:
    if len(manifest["targets"]) >= NT:
        break
    if i in used:
        continue
    used.add(i)
    z = key(Ylist[i], t)
    ri = next(q for q, h in enumerate(holdout) if key(h, t) != z and all(L.get((tt, key(h, tt)), 0) < C for tt in range(len(tables))))
    rep = holdout.pop(ri)
    tid = len(manifest["targets"])
    tdir = os.path.join(out, f"target{tid}")
    os.makedirs(tdir, exist_ok=True)
    inj = []
    for j in range(K):
        text, _ = craft(t, z, seed=1000 * tid + j)
        p = os.path.join(tdir, f"injected_{j}.py")
        open(p, "w").write(text)
        fp = np.array(simhash64(open(p).read()), dtype=np.uint8)  # fingerprint again from disk
        assert key(fp, t) == z
        inj.append(fp)
    Y1 = Ylist + inj
    Y0 = Ylist[:i] + [rep] + Ylist[i + 1:] + inj
    for name, Ys in (("H1", Y1), ("H0", Y0)):
        uniq = {bytes(y): y for y in Ys}
        write_bin(os.path.join(tdir, f"{name}.bin"), X, list(uniq.values()))
        LL = loads(list(uniq.values()))
        manifest_entry = dict(size=len(uniq), max_load=max(LL.values()), target_bucket_load=LL.get((t, z), 0))
        print(tid, name, manifest_entry, flush=True)
        if name == "H1":
            e1 = manifest_entry
        else:
            e0 = manifest_entry
    manifest["targets"].append(dict(id=tid, target_path=Yfp[bytes(Ylist[i])], table=t, H1=e1, H0=e0))
json.dump(manifest, open(os.path.join(out, "manifest.json"), "w"), indent=1)
print("done", len(manifest["targets"]))
