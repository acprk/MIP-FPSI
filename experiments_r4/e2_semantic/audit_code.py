"""E2: semantic audit of the CPython code-screening workload.

For each threshold delta, which sender files (CPython 3.11.9) are output, and are they useful review
candidates? Ground truth is independent of the fingerprints:
  * line similarity: difflib ratio on the line sequences of a matched (sender, receiver) file pair;
  * provenance: same relative path in the two releases.
A candidate (sender file with at least one receiver file within delta) is "useful" if some matched
receiver file shares substantial content (line ratio >= 0.5). Recall is measured against the set G of
sender files whose same-path successor has line ratio >= 0.8 (derived near-copies).
Outputs results/code_audit.json and a stratified sample of incremental matches for manual reading.
usage: python3 audit_code.py <Python-3.11.9.tgz> <Python-3.12.4.tgz> <outdir>
"""
import difflib
import json
import os
import random
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../../data"))
from build_code_simhash import load, simhash64  # noqa: E402  (same fingerprint pipeline as the paper)

a_tgz, b_tgz, out = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
A, Bf = load(a_tgz), load(b_tgz)
ka, kb = sorted(A), sorted(Bf)
XA = np.array([simhash64(A[k]) for k in ka], dtype=np.uint8)
YB = np.array([simhash64(Bf[k]) for k in kb], dtype=np.uint8)
D = (XA[:, None, :] != YB[None, :, :]).sum(2)  # 3199 x 3170 Hamming distances

ratio_cache = {}


def ratio(i, j):
    key = (i, j)
    if key not in ratio_cache:
        la, lb = A[ka[i]].splitlines(), Bf[kb[j]].splitlines()
        ratio_cache[key] = difflib.SequenceMatcher(None, la, lb, autojunk=False).ratio()
    return ratio_cache[key]


bidx = {k: j for j, k in enumerate(kb)}
same_path = {i: bidx.get(ka[i]) for i in range(len(ka))}
G = {i for i, j in same_path.items() if j is not None and ratio(i, j) >= 0.8}

res = {"m_files": len(ka), "n_files": len(kb), "derived_near_copies_G": len(G), "by_delta": {}}
for delta in range(0, 9):
    cand = [i for i in range(len(ka)) if (D[i] <= delta).any()]
    pairs = int((D <= delta).sum())
    useful, samepath, junk = 0, 0, 0
    for i in cand:
        js = np.nonzero(D[i] <= delta)[0]
        best = max(ratio(i, int(j)) for j in js[:50])
        useful += best >= 0.5
        junk += best < 0.2
        samepath += same_path[i] is not None and same_path[i] in set(js.tolist())
    rec = sum(1 for i in G if (D[i] <= delta).any() and (D[i, same_path[i]] <= delta))
    res["by_delta"][delta] = dict(candidates=len(cand), pairs=pairs, useful=useful,
                                  precision=useful / max(1, len(cand)), same_path=samepath,
                                  unrelated=junk, recall_G=rec / len(G))
    print(delta, res["by_delta"][delta], flush=True)

# incremental matches at the paper's threshold: output at delta=3 but not by exact equality
inc = [i for i in range(len(ka)) if (D[i] <= 3).any() and not (D[i] == 0).any()]
cats = {"same_path_successor": 0, "other_file_similar": 0, "unrelated": 0}
rows = []
for i in inc:
    js = [int(j) for j in np.nonzero(D[i] <= 3)[0]]
    sp = same_path[i] in js
    best_j = max(js[:50], key=lambda j: ratio(i, j))
    r = ratio(i, best_j)
    cat = "same_path_successor" if sp else ("other_file_similar" if r >= 0.5 else "unrelated")
    cats[cat] += 1
    rows.append(dict(sender=ka[i], best_match=kb[best_j], hamming=int(D[i, best_j]), line_ratio=round(r, 3),
                     category=cat, sender_lines=len(A[ka[i]].splitlines())))
res["incremental_delta3"] = dict(count=len(inc), categories=cats)
random.seed(7)
sample = []
for c in cats:
    pool = [r for r in rows if r["category"] == c]
    sample += random.sample(pool, min(10, len(pool)))
res["incremental_sample"] = sample
json.dump(res, open(os.path.join(out, "code_audit.json"), "w"), indent=1)
with open(os.path.join(out, "incremental_delta3.tsv"), "w") as f:
    f.write("sender\tbest_match\thamming\tline_ratio\tcategory\tsender_lines\n")
    for r in rows:
        f.write("\t".join(str(r[k]) for k in ("sender", "best_match", "hamming", "line_ratio", "category", "sender_lines")) + "\n")
print(json.dumps(res["incremental_delta3"], indent=1))
