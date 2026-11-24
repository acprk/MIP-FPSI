# Round 4 — tightening the 2^δ gap of the size-only bound (falsifiable claims)

Date 2026-11-10. Setting = probe-and-verify model of paper Sec. V (fixed public cover, capacities fixed by
public parameters + leakage, full probing, one test per slot, FN prob ε). Size-only leakage.

## Definitions
* Bucket of table t with r = 0: receiver vectors agreeing with x on S_t; they differ only on E_t = [d]\S_t,
  e_t = |E_t|. Inside a bucket the problem lives in the cube {0,1}^{e}.
* PW(e,δ) = max |P|, P ⊆ {0,1}^e, such that every p∈P has a witness x_p with Ball(x_p,δ) ∩ P = {p}
  (= upper irredundance number of the δ-th distance power of Q_e, closed neighbourhoods).
* τ(e,δ) = max over W ⊆ {0,1}^e of the minimum size of P ⊆ W with N_δ(P) ⊇ N_δ(W)
  (capacity a size-only receiver needs in one bucket of a radius-0 table).

## Claims
* C1 (τ = PW). A minimum dominating subset is irredundant ⇒ τ ≤ PW; an irredundant W needs all of itself ⇒ τ ≥ PW.
* C2 (lower). PW(e,δ) ≥ 2^{e-δ} for e ≥ δ (fix δ coordinates, free on the rest; witness = flip the δ coords).
* C3 (upper, proved). PW(e,δ) ≤ 2^{e-δ+1}. Proof: S_p = {z : H(z,p)+H(z,x_p) ≤ δ} are pairwise disjoint
  (z ∈ S_p∩S_q ⇒ H(q,x_p)+H(p,x_q) ≤ 2δ ⇒ one ≤ δ, contradicting privacy), and
  |S_p| = 2^k V(e-k, ⌊(δ-k)/2⌋) ≥ 2^{δ-1}, k = H(p,x_p), using V(j,⌊j/2⌋) ≥ 2^{j-1}.
* C3' (conjecture). PW(e,δ) = 2^{e-δ} exactly. Evidence: MILP exact for (3,1),(4,1),(4,2),(5,2),(5,3).
* C4 (per-cover characterisation). For every radius-0 minimal cover with blocks E_t:
  (1-ε) Σ_t min(n, 2^{e_t-δ}) ≤ OPT_sizeonly(cover) ≤ Σ_t min(n, 2^{e_t-δ+1}).
  Lower = paper Thm 1 (r=0 ⇒ Γ_t = {∅}, β_t = 2^{e_t-δ}). Upper = receiver places a minimum dominating subset
  of each bucket (capacity from C3), zero FN for every input.
* C5 (optimum over radius-0 minimal covers). With f(e) = 2^{e-δ}/C(e,δ), min_{e≥δ} f = f(2δ) = 2^δ/C(2δ,δ):
  (1-ε) min(n, 2^δ C(d,δ)/C(2δ,δ)) ≤ OPT_0 ≤ min(n, 2^{δ+1} C(d,2δ,δ)),
  C(d,2δ,δ) = covering number; ≤ (1+ln C(2δ,δ)) C(d,δ)/C(2δ,δ) (greedy), = (1+o(1)) C(d,δ)/C(2δ,δ) (Rödl, d→∞).
  ⇒ OPT_0 = Θ(min(n, √δ C(d,δ)/2^δ)), constant 2(1+o(1)).
* C6 (all minimal covers). Old Cor. 2 lower bound V(d,δ)/2^δ stays; the C5 upper bound applies ⇒
  gap ≤ 4√δ (1+o(1)) instead of 2^δ (C(2δ,δ) ≥ 4^δ/(2√δ)).

## Kill tests
* K1: find P ⊆ {0,1}^e irredundant (radius δ) with |P| > 2^{e-δ+1}  → kills C3.
* K2: find P with |P| > 2^{e-δ}  → kills C3' only.
* K3: end-to-end: radius-0 cover + capacity min(n,2^{e-δ+1}) + minimum dominating placement misses a match.
* K4: minimal r=0 cover whose critical tables do not cover all δ-sets (would break the LP step of C5).
* K5: minimiser of f(e) not at 2δ-1/2δ.
