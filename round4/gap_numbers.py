"""Explicit size-only gap (upper/lower) at d=128 using the greedy covering bound."""
import math

def V(n, r):
    return sum(math.comb(n, i) for i in range(r + 1))

def greedy(d, e, t):
    return (1 + math.log(math.comb(e, t))) * math.comb(d, t) / math.comb(e, t)

for d, dl in [(128, 4), (128, 8), (128, 16)]:
    lower = V(d, dl) / 2 ** dl
    manku = math.comb(d, dl)
    old = min(manku, 2 * greedy(d, dl + 1, dl))
    new_2d = 2 ** (dl + 1) * greedy(d, 2 * dl, dl)
    best_any = min([manku, 2 * greedy(d, dl + 1, dl)] +
                   [2 ** (e - dl + 1) * greedy(d, e, dl) for e in range(dl + 2, 3 * dl + 1)])
    print(f"d={d} delta={dl}: old gap {old/lower:.1f}  new(e=2delta) gap {new_2d/lower:.1f}  "
          f"best over e {best_any/lower:.1f}  radius-0 lower {2**dl*math.comb(d,dl)/math.comb(2*dl,dl):.4g} "
          f"= 2^{math.log2(2**dl*math.comb(d,dl)/math.comb(2*dl,dl)):.2f}; Cor2 2^{math.log2(lower):.2f}")
