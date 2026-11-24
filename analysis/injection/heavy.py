"""Top competing buckets of each dataset (what the injection has to outgrow).

usage: heavy.py -> results/heavy_buckets.csv
"""
import csv
import os

import numpy as np

import datasets
from common import Loads

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    rows = []
    for name in datasets.CONFIGS:
        Y, _, T, meta = datasets.get(name)
        L = Loads(Y, T)
        allb = sorted(((c, t, key) for t, srt in enumerate(L.sorted) for c, key in srt[:5]),
                      reverse=True)[:5]
        for rank, (c, t, key) in enumerate(allb):
            idx = np.nonzero(L.keys[t] == np.uint64(key))[0]
            w = len(T[t])
            rows.append(dict(dataset=name, rank=rank + 1, table=t, keybits=w, load=c,
                             key_hex=format(key, f"0{(w + 3) // 4}x"),
                             key_popcount=bin(key).count("1"),
                             mean_weight_of_members=round(float(Y[idx].sum(axis=1).mean()), 1)))
    with open(os.path.join(HERE, "results", "heavy_buckets.csv"), "w", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        wr.writeheader()
        wr.writerows(rows)
    for r in rows:
        print(r)


if __name__ == "__main__":
    main()
