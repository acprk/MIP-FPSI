"""Dataset configurations for the injection evaluation."""
import os

import numpy as np

from common import dedup, load_bin, radius, tables, uniform

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA = os.path.join(ROOT, "data")

CONFIGS = {
    # name: (source, d, delta, b, s, n)
    "U128-n2^10": ("uniform", 128, 4, 5, 1, 1 << 10),
    "U128-n2^12": ("uniform", 128, 4, 5, 1, 1 << 12),
    "U64-n2^12": ("uniform", 64, 3, 4, 1, 1 << 12),
    "code-simhash64": ("code_simhash64.bin", 64, 3, 4, 1, None),
    "cifar-dhash64-mild": ("cifar_dhash64_mild.bin", 64, 4, 6, 2, None),
}


def get(name, seed=0):
    """Returns (Y, pool, T, meta). Y: receiver set (distinct rows); pool: replacement vectors
    for the b=0 hypothesis (fresh uniform vectors, or the sender-side set X of the same corpus)."""
    src, d, delta, b, s, n = CONFIGS[name]
    assert radius(b, s, delta) == 0, "only radius-0 (exact-key) covers"
    rng = np.random.default_rng(1000 + seed)
    if src == "uniform":
        Y = uniform(n, d, rng)
        pool = uniform(4096, d, rng)
    else:
        X, Y, dd = load_bin(os.path.join(DATA, src))
        assert dd == d
        Y = dedup(Y)
        pool = dedup(X)
    T = tables(d, b, s)
    meta = dict(dataset=name, d=d, delta=delta, b=b, s=s, n=len(Y), T=len(T),
                keybits="/".join(sorted({str(len(p)) for p in T})))
    return Y, pool, T, meta
