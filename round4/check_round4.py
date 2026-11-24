"""Kill tests K1, K2, K3, K5 of FALSIFY.md (pure Python, no dependencies)."""
import itertools
import math
import random
import sys

random.seed(20261110)


def pc(x):
    return bin(x).count("1")


def V(n, r):
    return sum(math.comb(n, i) for i in range(0, min(n, r) + 1)) if r >= 0 else 0


# ---------- irredundance in Q_e^delta ----------
def balls(e, d):
    N = 1 << e
    return [[q for q in range(N) if pc(p ^ q) <= d] for p in range(N)]


def is_irredundant(P, e, d, B):
    Ps = set(P)
    wit = {}
    for p in P:
        ok = None
        for x in B[p]:  # x within d of p
            if all((q == p) or (q not in Ps) for q in B[x]):
                ok = x
                break
        if ok is None:
            return None
        wit[p] = ok
    return wit


def local_search(e, d, iters, B):
    """Maximise |P| irredundant by randomised add/drop moves."""
    N = 1 << e
    best = []
    P = []
    for _ in range(iters):
        cand = random.randrange(N)
        if cand in P:
            continue
        Q = P + [cand]
        if is_irredundant(Q, e, d, B) is not None:
            P = Q
            if len(P) > len(best):
                best = list(P)
        elif P and random.random() < 0.3:
            P.pop(random.randrange(len(P)))
    return best


def k1_k2():
    print("K1/K2: randomised search for large irredundant sets")
    worst = 0
    for e, d in [(4, 1), (5, 1), (6, 1), (6, 2), (7, 2), (7, 3), (8, 2), (8, 3), (8, 4), (9, 3)]:
        B = balls(e, d)
        best = []
        for rep in range(6):
            P = local_search(e, d, 6000 if e <= 7 else 3000, B)
            if len(P) > len(best):
                best = P
        wit = is_irredundant(best, e, d, B)
        assert wit is not None
        # check region-disjointness lemma on the found set
        regions = {}
        for p, x in wit.items():
            S = [z for z in range(1 << e) if pc(z ^ p) + pc(z ^ x) <= d]
            k = pc(p ^ x)
            assert len(S) == (1 << k) * V(e - k, (d - k) // 2)
            assert len(S) >= 1 << (d - 1)
            for z in S:
                assert z not in regions, "region overlap -> C3 proof broken"
                regions[z] = p
        flag = "  >2^(e-d)!" if len(best) > 2 ** (e - d) else ""
        print(f"  e={e} d={d}: best found {len(best)}  2^(e-d)={2**(e-d)}  2^(e-d+1)={2**(e-d+1)}{flag}")
        worst = max(worst, len(best) / 2 ** (e - d))
        assert len(best) <= 2 ** (e - d + 1), "K1 KILL"
    print(f"  max |P|/2^(e-d) found: {worst}")


# ---------- exhaustive tau for tiny cubes ----------
def tau_exhaustive(e, d):
    """max over W of min dominating subset size (dominating N_d(W)), all W."""
    N = 1 << e
    B = balls(e, d)
    ballmask = [sum(1 << q for q in B[p]) for p in range(N)]
    best = 0
    for Wm in range(1, 1 << N):
        W = [v for v in range(N) if Wm >> v & 1]
        target = 0
        for w in W:
            target |= ballmask[w]
        found = None
        for size in range(1, len(W) + 1):
            for P in itertools.combinations(W, size):
                m = 0
                for p in P:
                    m |= ballmask[p]
                if m == target:
                    found = size
                    break
            if found:
                break
        best = max(best, found)
    return best


# ---------- K3: end-to-end radius-0 cover with dominating placement ----------
def greedy_covering(dd, e, t):
    """greedy (dd, e, t) covering design: blocks of size e covering all t-subsets."""
    need = set(itertools.combinations(range(dd), t))
    blocks = []
    allblocks = list(itertools.combinations(range(dd), e))
    while need:
        bestb, bestc = None, -1
        sample = random.sample(allblocks, min(len(allblocks), 400))
        for b in sample:
            c = sum(1 for T in itertools.combinations(b, t) if T in need)
            if c > bestc:
                bestb, bestc = b, c
        if bestc <= 0:
            T = next(iter(need))
            rest = [i for i in range(dd) if i not in T]
            bestb = tuple(sorted(T + tuple(random.sample(rest, e - t))))
        blocks.append(bestb)
        for T in itertools.combinations(bestb, t):
            need.discard(T)
    # prune to an irredundant (minimal) cover
    changed = True
    while changed:
        changed = False
        for i in range(len(blocks)):
            others = blocks[:i] + blocks[i + 1:]
            cov = set()
            for b in others:
                cov.update(itertools.combinations(b, t))
            if all(T in cov for T in itertools.combinations(range(dd), t)):
                blocks = others
                changed = True
                break
    return blocks


def min_dominating(W, e_pos, d, cap):
    """W: list of vectors (ints over full d bits); domination inside the bucket = distance on e_pos."""
    def proj(v):
        return tuple((v >> i) & 1 for i in e_pos)
    pts = list({proj(w): w for w in W}.items())
    e = len(e_pos)
    # targets: all points of {0,1}^e within d of some W point
    def dist(a, b):
        return sum(x != y for x, y in zip(a, b))
    cube = list(itertools.product((0, 1), repeat=e))
    tgt = [z for z in cube if any(dist(z, a) <= d for a, _ in pts)]
    for size in range(1, len(pts) + 1):
        if size > cap:
            return None
        for P in itertools.combinations(pts, size):
            if all(any(dist(z, a) <= d for a, _ in P) for z in tgt):
                return [w for _, w in P]
    return [w for _, w in pts]


def k3(dd=9, d=2, trials=30):
    e = 2 * d
    blocks = greedy_covering(dd, e, d)
    cap = 2 ** (e - d + 1)
    print(f"K3: d={dd}, delta={d}, {len(blocks)} blocks of size {e}, capacity {cap}")
    N = 1 << dd
    for trial in range(trials):
        n = random.choice([20, 60, 120])
        if trial % 3 == 0:  # clustered: many vectors agreeing outside one block
            b = blocks[random.randrange(len(blocks))]
            base = random.randrange(N)
            Y = {base ^ sum(random.randint(0, 1) << i for i in b) for _ in range(n)}
        else:
            Y = set(random.sample(range(N), n))
        Y = list(Y)
        placed = []  # per table: dict key -> placed vectors
        maxload = 0
        for b in blocks:
            S = [i for i in range(dd) if i not in b]
            buckets = {}
            for y in Y:
                buckets.setdefault(tuple((y >> i) & 1 for i in S), []).append(y)
            pl = {}
            for k, W in buckets.items():
                P = min_dominating(W, list(b), d, cap)
                assert P is not None, "capacity exceeded -> C3 KILL"
                maxload = max(maxload, len(P))
                pl[k] = P
            placed.append((S, pl))
        # every x in the cube
        for x in range(N):
            truth = any(pc(x ^ y) <= d for y in Y)
            found = False
            for S, pl in placed:
                k = tuple((x >> i) & 1 for i in S)
                if any(pc(x ^ y) <= d for y in pl.get(k, [])):
                    found = True
                    break
            assert found == truth, f"K3 KILL: x={x} truth={truth} found={found}"
        print(f"  trial {trial}: n={len(Y)} ok, max placed per bucket {maxload}")


def k5():
    print("K5: argmin of 2^(e-delta)/C(e,delta)")
    for d in range(1, 13):
        vals = {e: 2 ** (e - d) / math.comb(e, d) for e in range(d, 6 * d + 2)}
        m = min(vals.values())
        arg = [e for e, v in vals.items() if abs(v - m) < 1e-12 * m]
        assert set(arg) <= {2 * d - 1, 2 * d}, arg
        assert abs(m - 2 ** d / math.comb(2 * d, d)) < 1e-12
        lo, hi = math.sqrt(math.pi * d) / 2 ** d, 2 * math.sqrt(d) / 2 ** d
        assert lo <= m * (1 + 1e-9) and m <= hi * (1 + 1e-9), (d, m, lo, hi)
        print(f"  delta={d}: argmin {arg}, min*2^d={m * 2**d:.3f}, sqrt(pi d)={math.sqrt(math.pi*d):.3f}")


if __name__ == "__main__":
    what = sys.argv[1:] or ["k5", "tau", "k1", "k3"]
    if "k5" in what:
        k5()
    if "tau" in what:
        for e, d in [(2, 1), (3, 1), (3, 2), (4, 1), (4, 2), (4, 3)]:
            t = tau_exhaustive(e, d)
            print(f"tau exhaustive e={e} d={d}: {t}  (2^(e-d)={2**(e-d)})")
            assert t <= 2 ** (e - d + 1)
    if "k1" in what:
        k1_k2()
    if "k3" in what:
        k3()
    print("all checks passed")
