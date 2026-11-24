"""Go/no-go for the redesigned candidate generation on real LFW FaceNet features.

For each binarization dimension d:
  * genuine / impostor Hamming distributions, threshold delta at target FAR, TAR
  * bit-sampling LSH (concatenate k' sampled bits): exact per-pair collision
    p(t) = C(d-t,k')/C(d,k'); over L independent subsets miss = (1-p)^L.
    For a miss target eps at t = delta, pick (k', L) minimising expected
    impostor candidates per sender element + L lookups.
  * pigeonhole multi-index (delta+1 disjoint blocks, zero false negatives):
    impostor candidate rate measured empirically.
"""
import sys
from math import comb, log, log1p, ceil

import numpy as np

rng = np.random.default_rng(7)
data = np.load("<HOME>/0401project/release/cm-faceid-src/data/lfw_all_features.npz", allow_pickle=True)
F = data["features"].astype(np.float64)
F /= np.linalg.norm(F, axis=1, keepdims=True)
persons = data["persons"]

# genuine pairs: all same-person pairs (capped per person)
idx_by_p = {}
for i, p in enumerate(persons):
    idx_by_p.setdefault(p, []).append(i)
gen = []
for ids in idx_by_p.values():
    if len(ids) < 2:
        continue
    ids = ids[:20]
    for a in range(len(ids)):
        for b in range(a + 1, len(ids)):
            gen.append((ids[a], ids[b]))
gen = np.array(gen)
N = len(F)
imp = rng.integers(0, N, size=(400000, 2))
imp = imp[persons[imp[:, 0]] != persons[imp[:, 1]]]
print(f"genuine pairs {len(gen)}, impostor pairs {len(imp)}")

N_REF = 2 ** 20  # receiver set size used to translate rates into candidate counts


def best_lsh(d, delta, eps, td_imp):
    best = None
    for kp in range(1, min(d - delta, 80) + 1):
        p1 = comb(d - delta, kp) / comb(d, kp)
        if p1 <= 1e-12:
            break
        if p1 >= 1:
            continue
        L = ceil(log(eps) / log1p(-p1))
        if L > 200000:
            continue
        p2 = np.array([comb(d - t, kp) / comb(d, kp) for t in range(d + 1)])
        rate = float(np.mean(1 - (1 - p2[td_imp]) ** L))
        cost = L + N_REF * rate  # lookups + impostor candidates per sender element
        if best is None or cost < best[-1]:
            best = (kp, L, p1, rate, cost)
    return best


for d in [128, 256, 512]:
    W = rng.standard_normal((F.shape[1], d))
    B = (F @ W > 0).astype(np.uint8)
    dg = (B[gen[:, 0]] != B[gen[:, 1]]).sum(1)
    di = (B[imp[:, 0]] != B[imp[:, 1]]).sum(1)
    print(f"\n=== d={d} (SimHash of FaceNet) genuine mean {dg.mean():.1f}±{dg.std():.1f}, "
          f"impostor mean {di.mean():.1f}±{di.std():.1f}")
    for far in [1e-3, 1e-4]:
        delta = int(np.quantile(di, far))
        while (di <= delta).mean() > far and delta > 0:
            delta -= 1
        tar = (dg <= delta).mean()
        far_real = (di <= delta).mean()
        print(f"  FAR<={far:g}: delta={delta} ({delta/d:.3f}d) TAR={tar:.4f} FAR={far_real:.2e}")
        for eps in [2.0 ** -40, 2.0 ** -20, 1e-3, 1e-2]:
            kp, L, p1, rate, cost = best_lsh(d, delta, eps, di)
            print(f"    LSH miss<={eps:.1e}: k'={kp} L={L} impostor-cand-rate={rate:.3e} "
                  f"-> per-elt cost at n=2^20 ~{cost:.3g} (vs all-pairs {N_REF})")
        nb = delta + 1
        if d // nb >= 1:
            blocks = np.array_split(np.arange(d), nb)
            hit = np.zeros(len(imp), dtype=bool)
            for blk in blocks:
                hit |= (B[imp[:, 0]][:, blk] == B[imp[:, 1]][:, blk]).all(1)
            print(f"    pigeonhole {nb} blocks of ~{d//nb} bits: impostor-cand-rate={hit.mean():.3e}")
sys.stdout.flush()
