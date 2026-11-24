"""Image near-duplicate dataset: 64-bit dHash of CIFAR-10 images.

Receiver database Y = 50k train images + the originals of the first K test images.
Sender queries X = all 10k test images after a benign transformation chain
(upscale to 64x64, JPEG q=60, 2-pixel crop, back to 32x32). The first K queries have their
original in Y (true near-duplicates); the rest are fresh images.
"""
import io
import json
import pickle
import struct
import sys
import tarfile
from collections import Counter

import imagehash
import numpy as np
from PIL import Image


def batches(tgz):
    with tarfile.open(tgz) as t:
        for mem in t.getmembers():
            name = mem.name.rsplit("/", 1)[-1]
            if name.startswith("data_batch") or name == "test_batch":
                d = pickle.load(t.extractfile(mem), encoding="bytes")
                yield name, d[b"data"].reshape(-1, 3, 32, 32).transpose(0, 2, 3, 1)


def dhash_bits(img):
    h = imagehash.dhash(img, hash_size=8).hash.flatten()
    return h.astype(np.uint8)


def transform(img, crop=True):
    big = img.resize((64, 64), Image.BICUBIC)
    buf = io.BytesIO()
    big.save(buf, "JPEG", quality=60)
    j = Image.open(io.BytesIO(buf.getvalue()))
    if crop:
        j = j.crop((2, 2, 62, 62))
    return j.resize((32, 32), Image.BICUBIC)


def pack(rows):
    return np.packbits(np.asarray(rows, dtype=np.uint8), axis=1, bitorder="little")


if __name__ == "__main__":
    tgz, outp = sys.argv[1], sys.argv[2]
    K = int(sys.argv[3]) if len(sys.argv) > 3 else 5000
    train, test = [], None
    for name, arr in batches(tgz):
        if name == "test_batch":
            test = arr
        else:
            train.append(arr)
    train = np.concatenate(train)
    Y = [dhash_bits(Image.fromarray(a)) for a in train]
    Y += [dhash_bits(Image.fromarray(a)) for a in test[:K]]
    crop = (sys.argv[4] if len(sys.argv) > 4 else "crop") == "crop"
    X = [dhash_bits(transform(Image.fromarray(a), crop)) for a in test]
    Xp, Yp = pack(X), pack(Y)
    with open(outp, "wb") as f:
        f.write(struct.pack("<QQQ", len(Xp), len(Yp), 64))
        f.write(Xp.tobytes())
        f.write(Yp.tobytes())
    Xa, Ya = np.array(X), np.array(Y)
    gen = (Xa[:K] != Ya[len(train):]).sum(1)
    rng = np.random.default_rng(0)
    i = rng.integers(0, len(Xa), 50000)
    j = rng.integers(0, len(train), 50000)
    imp = (Xa[i] != Ya[j]).sum(1)
    stats = dict(m=len(Xp), n=len(Yp), K=K,
                 genuine_dist_hist={int(k): int(v) for k, v in sorted(Counter(gen.tolist()).items()) if k <= 16},
                 impostor_dist_mean=float(imp.mean()),
                 impostor_le={t: float((imp <= t).mean()) for t in range(0, 9)},
                 genuine_le={t: float((gen <= t).mean()) for t in range(0, 9)})
    json.dump(stats, open(outp + ".json", "w"), indent=1)
    print(json.dumps(stats, indent=1))
