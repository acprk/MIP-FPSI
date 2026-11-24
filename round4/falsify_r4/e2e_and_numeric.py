"""K3 end-to-end + numeric checks for C3, C5, C6 (own code).

Part A (K3): d=6, delta=2, radius-0 cover with blocks E_t = {0123},{0145},{2345}
(covering design C(6,4,2)), and d=7,delta=2 with blocks from a small covering.
For random Y (and Y = whole cube), receiver places in each bucket an exact minimum
subset P of the bucket W with N_delta(P) >= N_delta(W) (brute force), checks
|P| <= 2^(e-delta) (C3') and <= 2^(e-delta+1) (C3), then checks every x in {0,1}^d:
x has a match in Y  <=>  some probed bucket holds a placed vector within delta.
Part B: V(j,floor(j/2)) >= 2^(j-1), argmin f(e), C(2d,d) >= 4^d/(2 sqrt d), C6 gap.
Part C: every minimal radius-0 cover of [d] (small d) has all e_t >= delta.
"""
import itertools, random, math
from math import comb

def pc(x): return bin(x).count("1")

def proj(y, coords):
    v = 0
    for i, c in enumerate(coords):
        v |= ((y >> c) & 1) << i
    return v

def min_dom(W, e, d):
    N = 1 << e
    NW = set(x for x in range(N) if any(pc(x ^ w) <= d for w in W))
    W = list(W)
    for k in range(0, len(W) + 1):
        for P in itertools.combinations(W, k):
            if all(any(pc(x ^ p) <= d for p in P) for x in NW):
                return list(P)

def e2e(dim, delta, blocks, trials, rng, nmax=None, full=True):
    worst = 0
    for tr in range(trials):
        if tr == 0 and full:
            Y = list(range(1 << dim))
        else:
            n = rng.randint(1, nmax or (1 << dim))
            Y = rng.sample(range(1 << dim), n)
        placed = []  # per table dict key->list of y
        for E in blocks:
            S = [c for c in range(dim) if c not in E]
            e = len(E)
            buckets = {}
            for y in Y:
                buckets.setdefault(proj(y, S), []).append(y)
            tab = {}
            for z, ys in buckets.items():
                Wp = {proj(y, E): y for y in ys}
                P = min_dom(set(Wp), e, delta)
                worst = max(worst, len(P))
                assert len(P) <= 2 ** (e - delta + 1), ("C3 violated", len(P))
                tab[z] = [Wp[p] for p in P]
            placed.append((S, tab))
        for x in range(1 << dim):
            truth = any(pc(x ^ y) <= delta for y in Y)
            got = any(pc(x ^ y) <= delta for (S, tab) in placed for y in tab.get(proj(x, S), []))
            assert truth == got, ("FN/FP", x)
    return worst

rng = random.Random(20261110)
print("K3 d=6 delta=2 blocks C(6,4,2): worst capacity used =",
      e2e(6, 2, [[0, 1, 2, 3], [0, 1, 4, 5], [2, 3, 4, 5]], 200, rng), "(2^(e-d)=4)")
# d=7, delta=2: blocks of size 4 covering all pairs of [7]: Fano complements {lines' complements} size 4
fano = [[0, 1, 2], [0, 3, 4], [0, 5, 6], [1, 3, 5], [1, 4, 6], [2, 3, 6], [2, 4, 5]]
comp = [[c for c in range(7) if c not in L] for L in fano]
cov = all(any(set(D) <= set(B) for B in comp) for D in itertools.combinations(range(7), 2))
print("Fano-complement blocks cover all 2-sets of [7]:", cov)
print("K3 d=7 delta=2 Fano complements: worst capacity used =", e2e(7, 2, comp, 60, rng), "(2^(e-d)=4)")
print("K3 d=6 delta=3 blocks {012345} (all pairs): worst =", e2e(6, 3, [list(range(6))], 40, rng, nmax=14), "(2^(e-d)=8)")
print("K3 d=6 delta=2 blocks size 5 (C(6,5,2) needs ... use 3 blocks): worst =",
      e2e(6, 2, [[0, 1, 2, 3, 4], [0, 1, 2, 3, 5], [0, 1, 2, 4, 5], [0, 3, 4, 5, 1], [2, 3, 4, 5, 0]], 30, rng, nmax=24, full=False), "(2^(e-d)=8)")

# Part B
okV = all(sum(comb(j, i) for i in range(j // 2 + 1)) * 2 >= 2 ** j for j in range(0, 400))
print("V(j,floor(j/2)) >= 2^(j-1) for j<400:", okV)
for dl in range(1, 21):
    f = {e: 2 ** (e - dl) / comb(e, dl) for e in range(dl, 6 * dl + 2)}
    m = min(f.values())
    args = [e for e in f if abs(f[e] - m) < 1e-12 * m]
    assert args == [2 * dl - 1, 2 * dl] or (dl == 1 and args == [1, 2]), (dl, args)
print("argmin f = {2d-1,2d} for delta=1..20: True")
print("C(2d,d)>=4^d/(2 sqrt d) for d=1..300:",
      all(comb(2 * d, d) * 2 * math.sqrt(d) >= 4 ** d * (1 - 1e-12) for d in range(1, 300)))
for dl in (4, 8, 16):
    ratio = 2 * 4 ** dl / comb(2 * dl, dl)
    print(f"delta={dl}: asymptotic gap 2*4^d/C(2d,d)={ratio:.3f}  4sqrt(d)={4*math.sqrt(dl):.3f}  old 2^d={2**dl}  "
          f"greedy factor 1+ln C(2d,d)={1+math.log(comb(2*dl,dl)):.2f} -> explicit gap <= {ratio*(1+math.log(comb(2*dl,dl))):.1f}")
# concrete d=128 comparison: explicit (greedy-bound) upper / Cor.2 lower vs old C(d,delta)/(V/2^delta)
for (D, dl) in ((128, 4), (128, 8), (128, 16), (256, 8)):
    V = sum(comb(D, i) for i in range(dl + 1))
    low = V / 2 ** dl
    old_up = comb(D, dl)
    new_up = 2 ** (dl + 1) * (1 + math.log(comb(2 * dl, dl))) * comb(D, dl) / comb(2 * dl, dl)
    sch = 1.0  # Schonheim lower bound on C(D,2dl,dl) for reference: product ceil
    print(f"d={D} delta={dl}: lower V/2^d={low:.3e} old upper C(d,delta)={old_up:.3e} (gap {old_up/low:.1f}) "
          f"new upper(greedy, explicit)={new_up:.3e} (gap {new_up/low:.1f}); new upper (Rodl, o(1)=0) gap {2*4**dl/comb(2*dl,dl)*comb(D,dl)/V:.2f}")

# Part C: minimal radius-0 covers on small d have all e_t >= delta
def is_cover(blocks, dim, dl):
    return all(any(set(D) <= B for B in blocks) for D in itertools.combinations(range(dim), dl))
def is_minimal(blocks, dim, dl):
    for t, B in enumerate(blocks):
        crit = False
        for k in range(0, dl + 1):
            for D in itertools.combinations(sorted(B), k):
                if sum(1 for B2 in blocks if set(D) <= B2) == 1:
                    crit = True; break
            if crit: break
        if not crit: return False
    return True
bad = 0; cnt = 0
for dim in (4, 5):
    subsets = [frozenset(s) for k in range(0, dim + 1) for s in itertools.combinations(range(dim), k)]
    for dl in range(1, dim):
        for T in range(1, 5):
            for blocks in itertools.combinations(subsets, T):
                if is_cover(blocks, dim, dl) and is_minimal(blocks, dim, dl):
                    cnt += 1
                    if min(len(B) for B in blocks) < dl:
                        bad += 1
print(f"Part C: minimal r=0 covers (d<=5, T<=4) enumerated={cnt}, with some e_t<delta: {bad}")
