"""E2b: semantic audit of the CIFAR-10 workload (mild re-encoding, delta=4).

Outputs are sender (re-encoded test) images within delta of some receiver image. For each output we
check whether one of its matches is its own original (a true near-duplicate by construction); for the
others we measure pixel similarity between the sender's original test image and the matched receiver
image, and save a montage for visual inspection.
usage: python3 audit_cifar.py <cifar-10-python.tar.gz> <outdir>
"""
import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../../data"))
from build_image_dhash import batches, dhash_bits, transform  # noqa: E402

tgz, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
K, DELTA = 5000, 4
train, test = [], None
for name, arr in batches(tgz):
    if name == "test_batch":
        test = arr
    else:
        train.append(arr)
train = np.concatenate(train)
Yimg = np.concatenate([train, test[:K]])
Y = np.array([dhash_bits(Image.fromarray(a)) for a in Yimg], dtype=np.uint8)
X = np.array([dhash_bits(transform(Image.fromarray(a), crop=False)) for a in test], dtype=np.uint8)
Yp = np.packbits(Y, axis=1, bitorder="little").view(np.uint64).ravel()
Xp = np.packbits(X, axis=1, bitorder="little").view(np.uint64).ravel()


def popcount(v):
    v = v.copy()
    c = np.zeros(v.shape, dtype=np.int64)
    while v.any():
        c += (v & np.uint64(1)).astype(np.int64)
        v >>= np.uint64(1)
    return c


rows = []
for i in range(len(Xp)):
    dist = popcount(Xp[i] ^ Yp)
    js = np.nonzero(dist <= DELTA)[0]
    if len(js) == 0:
        continue
    own = i < K and (len(train) + i) in set(js.tolist())
    best = min(js.tolist(), key=lambda j: np.abs(test[i].astype(int) - Yimg[j].astype(int)).mean())
    mad = float(np.abs(test[i].astype(int) - Yimg[best].astype(int)).mean())
    rows.append(dict(sender=i, own_original=bool(own), matches=len(js), best=int(best),
                     mean_abs_pixel_diff=round(mad, 2), dist=int(dist[best])))
own = [r for r in rows if r["own_original"]]
other = [r for r in rows if not r["own_original"]]
vis_dup = [r for r in other if r["mean_abs_pixel_diff"] <= 12]
res = dict(outputs_records=len(rows), with_own_original=len(own), other=len(other),
           other_visual_near_duplicate=len(vis_dup),
           other_mad_quartiles=np.percentile([r["mean_abs_pixel_diff"] for r in other], [25, 50, 75]).tolist() if other else [],
           genuine_mad_median=float(np.median([np.abs(test[r["sender"]].astype(int) - Yimg[len(train) + r["sender"]].astype(int)).mean() for r in own])))
json.dump(dict(summary=res, other=other), open(os.path.join(out, "cifar_audit.json"), "w"), indent=1)
print(json.dumps(res, indent=1))
# montage: each row = sender original | matched receiver image, for the 'other' outputs
tiles = []
for r in other[:60]:
    a = Image.fromarray(test[r["sender"]]).resize((64, 64))
    b = Image.fromarray(Yimg[r["best"]]).resize((64, 64))
    t = Image.new("RGB", (132, 64), "white")
    t.paste(a, (0, 0)); t.paste(b, (68, 0))
    tiles.append(t)
cols = 6
W = Image.new("RGB", (cols * 136, ((len(tiles) + cols - 1) // cols) * 68), "white")
for k, t in enumerate(tiles):
    W.paste(t, ((k % cols) * 136, (k // cols) * 68))
W.save(os.path.join(out, "cifar_other_matches.png"))
