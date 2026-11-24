"""Independent SAT check of PW(e,delta) (different tool from pw_milp.py / HiGHS).

PW(e,d) = max |P|, P subset {0,1}^e, each p in P has witness x with Ball(x,d) cap P = {p}.
Query: is there such P with |P| >= K ?  Encoded as CNF, solved with minisat.
Symmetry break: 0 in P (Q_e is vertex-transitive and the property is invariant).
Additionally each solution found is re-verified by brute force in Python.

usage: python3 pw_sat.py e delta K [timeout_s]
prints SAT/UNSAT/TIMEOUT
"""
import sys, subprocess, os, tempfile

def pc(x):
    return bin(x).count("1")

def main():
    e, d, K = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    tmo = int(sys.argv[4]) if len(sys.argv) > 4 else 600
    N = 1 << e
    ball = [[q for q in range(N) if pc(p ^ q) <= d] for p in range(N)]
    nv = 0
    def new():
        nonlocal nv
        nv += 1
        return nv
    a = [new() for _ in range(N)]
    w = {}
    for p in range(N):
        for x in ball[p]:
            w[(p, x)] = new()
    cl = []
    cl.append([a[0]])
    for p in range(N):
        cl.append([-a[p]] + [w[(p, x)] for x in ball[p]])
        for x in ball[p]:
            cl.append([-w[(p, x)], a[p]])
            for q in ball[x]:
                if q != p:
                    cl.append([-w[(p, x)], -a[q]])
    # at least K of a  <=>  at most N-K of (not a): sequential counter (Sinz)
    M = N - K
    lits = [-a[i] for i in range(N)]
    if M < 0:
        print("UNSAT(trivial)")
        return
    if M == 0:
        for l in lits:
            cl.append([-l])
    else:
        s = [[new() for _ in range(M)] for _ in range(N)]
        cl.append([-lits[0], s[0][0]])
        for j in range(1, M):
            cl.append([-s[0][j]])
        for i in range(1, N):
            cl.append([-lits[i], s[i][0]])
            cl.append([-s[i - 1][0], s[i][0]])
            for j in range(1, M):
                cl.append([-lits[i], -s[i - 1][j - 1], s[i][j]])
                cl.append([-s[i - 1][j], s[i][j]])
            cl.append([-lits[i], -s[i - 1][M - 1]])
    fd, cnf = tempfile.mkstemp(suffix=".cnf", dir=os.path.dirname(os.path.abspath(__file__)))
    out = cnf + ".out"
    with os.fdopen(fd, "w") as f:
        f.write(f"p cnf {nv} {len(cl)}\n")
        for c in cl:
            f.write(" ".join(map(str, c)) + " 0\n")
    try:
        r = subprocess.run(["nice", "-n", "10", "minisat", f"-cpu-lim={tmo}", cnf, out],
                           capture_output=True, text=True)
        txt = open(out).read().split() if os.path.exists(out) else ["INDET"]
    finally:
        for fn in (cnf, out):
            if os.path.exists(fn):
                os.remove(fn)
    if txt[0] == "SAT":
        vals = set(int(v) for v in txt[1:] if int(v) > 0)
        P = [p for p in range(N) if a[p] in vals]
        Ps = set(P)
        ok = all(any(set(q for q in ball[x] if q in Ps) == {p} for x in ball[p]) for p in P)
        print(f"e={e} d={d} K={K}: SAT |P|={len(P)} verified={ok} P={P}")
    elif txt[0] == "UNSAT":
        print(f"e={e} d={d} K={K}: UNSAT")
    else:
        print(f"e={e} d={d} K={K}: TIMEOUT/INDET")

main()
