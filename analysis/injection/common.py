"""Shared code for the active-injection evaluation of the load leakage B(Y).

Block cover: public permutation (seed 1, same construction as analysis/bu_stats.py and
impl/src/mip.cpp up to the seed), b contiguous blocks, every s-subset of blocks is a table,
radius r(b,s,delta) must be 0 (exact-key covers only).  key_t(y) = y restricted to the table's
coordinates, packed into an integer.  B(Y) = max_t max_z #{y in Y : key_t(y) = z}.

Leakage modes (what the sender S sees):
  exact     B
  rounded   ceil(log2 B)
  bound     [B > Bbar]                 (one bit, Bbar public)
  dp        B + max(0, s + Z)          Z two-sided geometric, P(Z=z) ~ exp(-eps|z|)
  dprounded ceil(log2(B + max(0, s + Z)))
"""
import math
import struct
from itertools import combinations

import numpy as np

DELTA_DP = 2.0 ** -30
MODES = ["exact", "rounded", "bound", "dp", "dprounded"]


# ----------------------------------------------------------------------------- data
def load_bin(path):
    with open(path, "rb") as f:
        m, n, d = struct.unpack("<QQQ", f.read(24))
        X = np.frombuffer(f.read(m * d // 8), dtype=np.uint8).reshape(m, d // 8)
        Y = np.frombuffer(f.read(n * d // 8), dtype=np.uint8).reshape(n, d // 8)
    unpack = lambda A: np.unpackbits(A, axis=1, bitorder="little")
    return unpack(X), unpack(Y), int(d)


def dedup(A):
    return np.unique(A, axis=0)


def uniform(n, d, rng):
    Y = dedup(rng.integers(0, 2, size=(n, d), dtype=np.uint8))
    while len(Y) < n:
        Y = dedup(np.vstack([Y, rng.integers(0, 2, size=(n - len(Y), d), dtype=np.uint8)]))
    return Y[rng.permutation(len(Y))]


# ----------------------------------------------------------------------------- cover
def radius(b, s, delta):
    q, rem = divmod(delta, b)
    return s * q + max(0, s + rem - b)


def tables(d, b, s, seed=1):
    perm = np.random.default_rng(seed).permutation(d)
    blocks = [perm[i * d // b:(i + 1) * d // b] for i in range(b)]
    return [np.sort(np.concatenate([blocks[i] for i in c])) for c in combinations(range(b), s)]


def keys_of(rows, pos):
    """rows: (N,d) uint8 bits -> uint64 keys of the coordinates pos (len(pos) <= 64)."""
    assert len(pos) <= 64
    w = (np.uint64(1) << np.arange(len(pos), dtype=np.uint64))
    return (rows[:, pos].astype(np.uint64) * w).sum(axis=1, dtype=np.uint64)


def clog2(x):
    """ceil(log2 x) for integer x >= 1."""
    return (int(x) - 1).bit_length()


# ----------------------------------------------------------------------------- leakage
def two_sided_geometric(eps, size, rng):
    """P(Z=z) proportional to exp(-eps|z|): difference of two iid geometrics on {0,1,...}."""
    p = 1.0 - math.exp(-eps)
    return (rng.geometric(p, size=size) - rng.geometric(p, size=size)).astype(np.int64)


def dp_shift(eps, delta_dp=DELTA_DP):
    return math.ceil(math.log(1.0 / delta_dp) / eps)


def leak(mode, B, rng=None, eps=None, Bbar=None, size=None):
    """Vectorised leakage of the true load B (int or array). For dp modes `size` noise draws."""
    if mode == "exact":
        return B
    if mode == "rounded":
        return clog2(B)
    if mode == "bound":
        return int(B > Bbar)
    s = dp_shift(eps)
    Z = two_sided_geometric(eps, size, rng)
    Bt = B + np.maximum(0, s + Z)
    if mode == "dp":
        return Bt
    if mode == "dprounded":
        return np.array([clog2(v) for v in Bt])
    raise ValueError(mode)


def decide(mode, lk, k, eps=None):
    """Attacker's decision 'target bucket has load k+1' (b=1) from leakage lk, k injected."""
    if mode == "exact":
        return lk >= k + 1
    if mode == "rounded":
        return lk >= clog2(k + 1)
    if mode == "bound":
        return lk == 1
    s = dp_shift(eps)
    if mode == "dp":
        return lk >= k + s + 1
    if mode == "dprounded":
        return lk >= clog2(k + s + 1)
    raise ValueError(mode)


def dp_tv(eps):
    """Exact total-variation distance between max(0,s+Z) and 1+max(0,s+Z) (= best advantage)."""
    q = math.exp(-eps)
    s = dp_shift(eps)
    # P(Z=0) is the TV of the unclipped shift; clipping only matters if Z < -s (prob <= q^s).
    return (1 - q) / (1 + q), q ** (s + 1) / (1 + q)


# ----------------------------------------------------------------------------- loads
class Loads:
    """Per-table bucket counts of a receiver set Y under a fixed cover."""

    def __init__(self, Y, T):
        self.Y = Y
        self.T = T
        self.keys = [keys_of(Y, pos) for pos in T]           # keys[t][i]
        self.cnt = []
        self.sorted = []                                       # per table: list of (count, key) desc
        for kt in self.keys:
            u, c = np.unique(kt, return_counts=True)
            self.cnt.append(dict(zip(u.tolist(), c.tolist())))
            o = np.argsort(-c, kind="stable")
            self.sorted.append(list(zip(c[o].tolist(), u[o].tolist())))
        self.B = max(s[0][0] for s in self.sorted)
        self.n = len(Y)

    def load1_pairs(self):
        """(i,t) such that y_i is the only vector with its key in table t."""
        out = []
        for t, kt in enumerate(self.keys):
            c = self.cnt[t]
            out += [(i, t) for i, z in enumerate(kt.tolist()) if c[z] == 1]
        return out

    def max_with(self, delta_by_table, exclude=None):
        """Max bucket load after adding per-table deltas {key: +-int}. exclude = (t, z) skipped."""
        best = 0
        for t, dl in enumerate(delta_by_table):
            for key, dv in dl.items():
                if exclude is not None and exclude == (t, key):
                    continue
                v = self.cnt[t].get(key, 0) + dv
                if v > best:
                    best = v
            for c, key in self.sorted[t]:
                if c <= best:
                    break
                if key in dl or (exclude is not None and exclude == (t, key)):
                    continue
                best = c
                break
        return best


def add_rows(delta, rows_keys, sign=1):
    """rows_keys: list over tables of uint64 key arrays; accumulate into delta dicts."""
    for t, kt in enumerate(rows_keys):
        dl = delta[t]
        for key in kt.tolist():
            dl[key] = dl.get(key, 0) + sign


class Injector:
    """Generates k distinct injected fingerprints with key z in table t (free coordinates random)."""

    def __init__(self, L, t, i, rng):
        self.L = L
        self.t = t
        self.i = i
        self.rng = rng
        self.d = L.Y.shape[1]
        self.rows = np.zeros((0, self.d), dtype=np.uint8)
        self.seen = {L.Y[i].tobytes()}

    def grow(self, k):
        pos = self.L.T[self.t]
        while len(self.rows) < k:
            need = k - len(self.rows)
            R = self.rng.integers(0, 2, size=(need, self.d), dtype=np.uint8)
            R[:, pos] = self.L.Y[self.i, pos]
            keep = []
            for j in range(need):
                bts = R[j].tobytes()
                if bts not in self.seen:
                    self.seen.add(bts)
                    keep.append(j)
            self.rows = np.vstack([self.rows, R[keep]])
        return self.rows[:k]


def scenario_loads(L, i, t, yrep, A):
    """True B under b=0 (y_i replaced by yrep) and b=1 (y_i present), plus the attacker-side
    competing max C = max load of (Y minus y_i) union A over all buckets except (t, z)."""
    nT = len(L.T)
    z = int(L.keys[t][i])
    yi_keys = [L.keys[tt][i:i + 1] for tt in range(nT)]
    A_keys = [keys_of(A, pos) for pos in L.T] if len(A) else [np.zeros(0, np.uint64)] * nT
    base = [dict() for _ in range(nT)]
    add_rows(base, yi_keys, -1)                 # remove target
    add_rows(base, A_keys, +1)                  # add injection
    C = L.max_with(base, exclude=(t, z))
    d1 = [dict(x) for x in base]
    add_rows(d1, yi_keys, +1)
    B1 = L.max_with(d1)
    d0 = [dict(x) for x in base]
    rep_keys = [keys_of(yrep[None, :], pos) for pos in L.T]
    assert int(rep_keys[t][0]) != z
    add_rows(d0, rep_keys, +1)
    B0 = L.max_with(d0)
    return B0, B1, C


def background_C(L, i, t):
    """Competing max load of Y minus y_i (no injection), excluding (t, key_t(y_i))."""
    return L.max_with(_minus_target(L, i), exclude=(t, int(L.keys[t][i])))


def _minus_target(L, i):
    delta = [dict() for _ in L.T]
    add_rows(delta, [L.keys[tt][i:i + 1] for tt in range(len(L.T))], -1)
    return delta


def pick_replacement(pool, pool_keys_t, z, rng, Yset):
    """Random vector of the replacement pool whose key in table t differs from z and not in Y."""
    while True:
        j = int(rng.integers(len(pool)))
        if int(pool_keys_t[j]) != z and pool[j].tobytes() not in Yset:
            return pool[j]


# ----------------------------------------------------------------------------- k choice (knowledge a)
def choose_k(mode, C, eps=None, Bbar=None):
    """Smallest injection size that makes the target bucket the unique maximum (k >= C+1) and
    puts the b=0/b=1 loads k, k+1 on the mode's decision boundary."""
    need = C + 1
    if mode in ("exact", "dp"):
        return need
    if mode == "rounded":
        return 1 << clog2(need)
    if mode == "bound":
        return Bbar
    if mode == "dprounded":
        s = dp_shift(eps)
        j = clog2(need + s)
        return (1 << j) - s
    raise ValueError(mode)


def wilson(p_hat, n, zq=1.96):
    if n == 0:
        return (float("nan"), float("nan"))
    den = 1 + zq * zq / n
    c = (p_hat + zq * zq / (2 * n)) / den
    h = zq * math.sqrt(p_hat * (1 - p_hat) / n + zq * zq / (4 * n * n)) / den
    return c - h, c + h
