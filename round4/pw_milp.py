"""PW(e,delta): max |W| in {0,1}^e such that every w in W has a witness p with
Ball(p,delta) ∩ W = {w}.  Conjecture: PW(e,delta) = 2^(e-delta) for e >= delta.
Solved exactly with scipy.optimize.milp (HiGHS)."""
import sys
import itertools
import numpy as np
from scipy.optimize import milp, LinearConstraint, Bounds
from scipy.sparse import lil_matrix


def popcount(x):
    return bin(x).count("1")


def pw(e, delta, aggregate=False, time_limit=600):
    N = 1 << e
    ball = [[q for q in range(N) if popcount(p ^ q) <= delta] for p in range(N)]
    pairs = [(v, p) for v in range(N) for p in ball[v]]
    nv = N + len(pairs)
    rows = []  # (dict coeffs, lb, ub)
    for k, (v, p) in enumerate(pairs):
        y = N + k
        rows.append(({y: 1, v: -1}, -np.inf, 0))
        others = [q for q in ball[p] if q != v]
        if aggregate:
            c = {q: 1 for q in others}
            c[y] = len(others)
            rows.append((c, -np.inf, len(others)))
        else:
            for q in others:
                rows.append(({y: 1, q: 1}, -np.inf, 1))
    by_v = {}
    for k, (v, p) in enumerate(pairs):
        by_v.setdefault(v, []).append(N + k)
    for v in range(N):
        c = {y: 1 for y in by_v[v]}
        c[v] = -1
        rows.append((c, 0, np.inf))
    A = lil_matrix((len(rows), nv))
    lb = np.empty(len(rows))
    ub = np.empty(len(rows))
    for i, (c, l, u) in enumerate(rows):
        for j, a in c.items():
            A[i, j] = a
        lb[i], ub[i] = l, u
    obj = np.zeros(nv)
    obj[:N] = -1
    lo = np.zeros(nv)
    lo[0] = 1  # symmetry: 0 in W
    res = milp(obj, constraints=LinearConstraint(A.tocsr(), lb, ub),
               integrality=np.ones(nv), bounds=Bounds(lo, np.ones(nv)),
               options={"time_limit": time_limit})
    W = [v for v in range(N) if res.x is not None and res.x[v] > 0.5]
    return res, W


if __name__ == "__main__":
    cases = [(int(a), int(b)) for a, b in (s.split(",") for s in sys.argv[1:])]
    for e, d in cases:
        res, W = pw(e, d, aggregate=(1 << e) * (1 << e) > 5000)
        print(f"e={e} delta={d} status={res.status} PW={len(W)} 2^(e-d)={2**(e-d)} "
              f"bound={-res.mip_dual_bound if hasattr(res,'mip_dual_bound') and res.mip_dual_bound is not None else 'na'}",
              flush=True)
