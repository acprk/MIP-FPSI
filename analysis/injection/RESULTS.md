# Active injection against the load leakage B(Y): measured advantage, injection budget, cost

Paper context: `paper/sections/security.tex` Sec. "Active Leakage Abuse" (attack game, DP countermeasure,
Prop. `prop:dp`) and `paper/sections/protocol.tex` Sec. "Leakage Modes".
Everything here is reproduced by `bash run_all.sh` (about 2 minutes on 4 cores, pinned to 44-47).
Raw per-trial CSVs and summaries are in `results/`.

## Files

| file | role |
|---|---|
| `common.py` | cover (seed-1 public permutation, same construction as `analysis/bu_stats.py`), keys, B(Y), the five leakage functions, decision rules, incremental load computation, injection generator |
| `datasets.py` | the five dataset configurations |
| `verify.py` | brute-force check: incremental B under both hypotheses = B recomputed from scratch on the explicit sets Y_b ∪ A; checks that the predicate differs and all sets have distinct vectors (`results/verify.log`: 0 mismatches in 200 random trials) |
| `attack_known.py` | knowledge (a), 2000 random targets per dataset → `results/known_*.csv` |
| `knowledge.py` | knowledge (b)/(c) settings and the blind k sweep → `results/{shadow,shadow2,uniform,prior,sweep}_*.csv` |
| `cost.py` | Q = B_revealed·T per mode → `results/cost.csv` |
| `heavy.py` | the heaviest competing buckets → `results/heavy_buckets.csv` |
| `summarize.py`, `make_tables.py` | `results/summary_*.csv`, `results/tables.md` (the tables below) |

## Methods (paragraph for the paper)

We evaluate the injection game of Sec. `sec:abuse` with radius-0 block covers fixed in advance: a public random
permutation of the d coordinates, b contiguous blocks, one table per s-subset of blocks. Hypotheses use replacement
adjacency. For each trial we draw a uniformly random pair (y, t) for which y is the only vector of Y with key z = key_t(y).
Y_1 = Y. Y_0 replaces y by a vector whose table-t key is not z: a fresh uniform vector for synthetic data, or a
vector of the sender-side set X of the same corpus for real data. The adversary injects k distinct
fingerprints with key z in table t and random free coordinates. It decides b from one observation of the
leakage. Under knowledge (a) it knows Y \ {y}. It computes the largest competing load C, the maximum over all
other buckets of (Y \ {y}) ∪ A, and takes the smallest k ≥ C + 1 that places the loads k and k + 1 on the mode's
decision boundary. That is k = C + 1 for exact and DP, the next power of two for rounded, k = B̄ for the public bound,
and k = 2^j − s for DP+rounded. With k ≥ C + 1 the unknown vector cannot lift a competing bucket above the target
bucket. B is then exactly k + b. The optimal DP test outputs 1 iff B̃ ≥ k + s + 1. We report the advantage
Pr[correct | b=1] + Pr[correct | b=0] − 1 over 2000 targets (DP: 100 noise draws per hypothesis and target),
with 95% intervals. The datasets are uniform d=128, δ=4, cover (5,1), n=2^10 and 2^12; uniform d=64, δ=3,
cover (4,1), n=2^12; code SimHash-64 (receiver set deduplicated, n=2959, (4,1), δ=3); and CIFAR-10 dHash-64 mild
(deduplicated, n=54994, (6,2), δ=4). All covers have radius 0. B̄ = 2B(Y) and δ_dp = 2^-30. Cost is Q = B_revealed·T
probes per sender vector (V(|S_t|,0)=1). For the DP modes we use the exact expectation over the noise.

## Results

### Datasets

| dataset | d | δ | cover (b,s) | key bits | T | n | B(Y) | targets with a load-1 bucket |
|---|---|---|---|---|---|---|---|---|
| U128-n2^10 | 128 | 4 | (5,1) | 25/26 | 5 | 1024 | 1 | 100.0% |
| U128-n2^12 | 128 | 4 | (5,1) | 25/26 | 5 | 4096 | 1 | 100.0% |
| U64-n2^12 | 64 | 3 | (4,1) | 16 | 4 | 4096 | 3 | 94.0% |
| code-simhash64 | 64 | 3 | (4,1) | 16 | 4 | 2959 | 29 | 78.2% |
| cifar-dhash64-mild | 64 | 4 | (6,2) | 20/21/22 | 15 | 54994 | 26 | 92.3% |

The last column is the fraction of (y, t) pairs for which the predicate can differ between the two hypotheses. Code
B = 29 matches `analysis/code_bu_stats.out` (dedup, (4,1)). The heavy real-data buckets are genuine near-duplicate clusters,
not degenerate all-zero hashes (`results/heavy_buckets.csv`).

### Table A: knowledge (a). Advantage / injection budget k / Q overhead relative to exact

C is the largest competing load the injection must exceed (median/max over targets).
95% CI half-width: 0 for the deterministic modes and ≤0.003 for DP.

| dataset | C | exact | rounded | bound (B̄=2B) | DP ε=1 | DP ε=0.1 | DP+rnd ε=1 | DP+rnd ε=0.1 |
|---|---|---|---|---|---|---|---|---|
| U128-n2^10 | 1/1 | 1.000 / 2 / 1.00× | 1.000 / 2 / 1.00× | 1.000 / 2 / 2.00× | 0.463 / 2 / 20.1× | 0.051 / 2 / 190× | 0.462 / 11 / 29.1× | 0.049 / 48 / 234× |
| U128-n2^12 | 1/1 | 1.000 / 2 / 1.00× | 1.000 / 2 / 1.00× | 1.000 / 2 / 2.00× | 0.461 / 2 / 16.0× | 0.049 / 2 / 150× | 0.458 / 11 / 22.8× | 0.049 / 48 / 184× |
| U64-n2^12 | 3/4 | 1.000 / 4 / 1.00× | 1.000 / 4 / 1.29× | 1.000 / 6 / 2.00× | 0.463 / 4 / 7.77× | 0.049 / 4 / 68.1× | 0.460 / 11 / 10.3× | 0.050 / 48 / 83.0× |
| code-simhash64 | 29/30 | 1.000 / 30 / 1.00× | 1.000 / 32 / 1.10× | 1.000 / 58 / 2.00× | 0.460 / 30 / 1.72× | 0.051 / 30 / 8.17× | 0.462 / 43 / 2.21× | 0.054 / 48 / 9.46× |
| cifar-dhash64-mild | 26/26 | 1.000 / 27 / 1.00× | 1.000 / 32 / 1.23× | 1.000 / 52 / 2.00× | 0.464 / 27 / 1.81× | 0.050 / 27 / 9.00× | 0.463 / 43 / 2.46× | 0.047 / 48 / 10.4× |

DP comparison. The best possible advantage against the release is the total-variation distance between max(0,s+Z) and
1+max(0,s+Z). It equals (1−q)/(1+q) = tanh(ε/2), plus a clipping term ≤ q^{s+1} ≈ 2^-30: **0.4621** (ε=1) and **0.0500** (ε=0.1).
The measured advantages agree with it within the CI on every dataset. The paper's bound e^ε − 1 + δ_dp = 1.718 (ε=1, vacuous) and
0.105 (ε=0.1) holds but is loose by a factor of 2 to 4. The attack is optimal, and it shows that the tight (ε,δ) bound
**(e^ε − 1 + 2δ_dp)/(e^ε + 1) ≈ tanh(ε/2)** is attained. Rounding the DP release does not reduce the advantage, because the
adversary sets k = 2^j − s so that the noise mode sits on a power-of-two boundary. It only raises k and the cost.

### Table B: how much background knowledge the attack needs (advantage, median k in parentheses)

Settings:
- (a) exact knowledge of Y \ {y} (Table A).
- (c) **prior run**: no data knowledge. One earlier non-injected run of the same mode reveals an upper bound on
  C: B, 2^⌈log2 B⌉, or B̃ ≥ B. Under DP this run costs a second ε.
- (b) **shadow**: a disjoint same-distribution, same-size sample gives Ĉ = B(shadow). Synthetic data use 20 fresh uniform
  sets. Real data split Y into random halves (receiver = one half, shadow = the other; 10 splits × 200 targets), so the
  receiver has n/2 vectors in this row.
- (b) **shadow ×2**: as shadow, but the attacker plans for load 2Ĉ.
- (b′) **uniform prior**: real data only. The attacker wrongly assumes uniform data of the same size.

| dataset | setting | exact | rounded | bound | DP ε=1 | DP ε=0.1 | DP+rnd ε=1 | DP+rnd ε=0.1 |
|---|---|---|---|---|---|---|---|---|
| U128-n2^12 | (c) prior run | 1.000 (2) | 1.000 (1) | 1.000 (2) | 0.461 (23) | 0.049 (210) | 0.463 (43) | 0.050 (304) |
| U128-n2^12 | (b) shadow | 1.000 (3) | 1.000 (4) | 1.000 (2) | 0.465 (3) | 0.052 (3) | 0.460 (11) | 0.048 (48) |
| U64-n2^12 | (c) prior run | 1.000 (4) | 1.000 (4) | 1.000 (6) | 0.458 (25) | 0.049 (212) | 0.463 (43) | 0.047 (304) |
| U64-n2^12 | (b) shadow | 1.000 (5) | 1.000 (8) | 1.000 (6) | 0.461 (5) | 0.049 (5) | 0.462 (11) | 0.050 (48) |
| code-simhash64 | (c) prior run | 1.000 (30) | 1.000 (32) | 1.000 (58) | 0.461 (51) | 0.051 (238) | 0.463 (107) | 0.053 (304) |
| code-simhash64 | (b) shadow | 0.699 (17) | 0.699 (32) | 1.000 (31) | 0.323 (17) | 0.035 (17) | 0.462 (43) | 0.050 (48) |
| code-simhash64 | (b) shadow ×2 | 1.000 (33) | 1.000 (64) | 1.000 (32) | 0.464 (33) | 0.048 (33) | 0.465 (43) | 0.049 (48) |
| code-simhash64 | (b′) uniform prior | 0.000 (4) | 0.000 (4) | 1.000 (30) | 0.000 (4) | −0.002 (4) | −0.001 (11) | 0.052 (48) |
| cifar-dhash64-mild | (c) prior run | 1.000 (27) | 1.000 (32) | 1.000 (52) | 0.463 (48) | 0.048 (235) | 0.464 (107) | 0.049 (304) |
| cifar-dhash64-mild | (b) shadow | 0.700 (16) | 0.900 (16) | 1.000 (30) | 0.327 (16) | 0.034 (16) | 0.460 (43) | 0.052 (48) |
| cifar-dhash64-mild | (b) shadow ×2 | 1.000 (29) | 1.000 (32) | 1.000 (30) | 0.463 (29) | 0.050 (29) | 0.462 (43) | 0.048 (48) |
| cifar-dhash64-mild | (b′) uniform prior | 0.000 (5) | 0.000 (8) | 1.000 (30) | −0.000 (5) | 0.000 (5) | 0.046 (11) | 0.049 (48) |

(The U128-n2^10 rows equal the n=2^12 rows within noise. See `results/tables.md`.)

### Table C: blind fixed-k sweep (300 targets per k, k = 1..40, 48, 64, 96, 128)

| dataset | competing load C (med/max) | exact: smallest k with adv ≥ 0.99 | adv at that k − 1 | all larger k work | rounded: working k |
|---|---|---|---|---|---|
| U128-n2^10 | 1/1 | 1 | – | yes | every power of two |
| U128-n2^12 | 1/1 | 1 | – | yes | every power of two |
| U64-n2^12 | 3/4 | 3 | 0.00 | yes | 4, 8, 16, … |
| code-simhash64 | 29/30 | 30 | 0.98 | yes | 32, 64, 128 |
| cifar-dhash64-mild | 26/26 | 26 | 0.00 | yes | 32, 64, 128 |

The advantage is a step function of k. It is 0 below the heaviest competing bucket and 1 from k ≈ C upwards (C + 1 is
always safe). For the rounded mode it is 1 exactly at powers of two ≥ C.

### Table D: protocol cost, probes per sender vector Q = B_revealed·T (ratio to exact)

Synthetic rows average B over 10 independent draws of Y. B̄ = 2B. DP rows use the exact noise expectation.

| dataset | T | B | exact | rounded | bound 2B | DP ε=1 | DP+rnd ε=1 | DP ε=0.1 | DP+rnd ε=0.1 |
|---|---|---|---|---|---|---|---|---|---|
| U128-n2^10 | 5 | 1.1 | 5.5 | 5.5 (1.00×) | 11 (2.00×) | 111 (20.1×) | 160 (29.1×) | 1046 (190×) | 1286 (234×) |
| U128-n2^12 | 5 | 1.4 | 7.0 | 7.0 (1.00×) | 14 (2.00×) | 112 (16.0×) | 160 (22.8×) | 1047 (150×) | 1286 (184×) |
| U64-n2^12 | 4 | 3.1 | 12.4 | 16 (1.29×) | 24.8 (2.00×) | 96 (7.77×) | 128 (10.3×) | 844 (68.1×) | 1029 (83.0×) |
| code-simhash64 | 4 | 29 | 116 | 128 (1.10×) | 232 (2.00×) | 200 (1.72×) | 256 (2.21×) | 948 (8.17×) | 1097 (9.46×) |
| cifar-dhash64-mild | 15 | 26 | 390 | 480 (1.23×) | 780 (2.00×) | 705 (1.81×) | 960 (2.46×) | 3510 (9.00×) | 4042 (10.4×) |

The DP extra cost is additive, E[max(0,s+Z)]·T ≈ s·T with s = 21 (ε=1) or 208 (ε=0.1). It is therefore cheap in relative
terms exactly where B is already large (clustered real data: 1.7–1.8× at ε=1, 8–9× at ε=0.1). It is very expensive for
uniform data with B ≈ 1–3 (16–20× and 150–190×).

## Findings

1. **Exact, rounded and public-bound leakage are fully broken by injection (advantage 1.000 on every dataset).** One
   run decides projection occupancy. The budget is k = C + 1 for exact: 2 for uniform 25-bit keys, 4 for 16-bit keys,
   30 for code SimHash, 27 for CIFAR dHash. Rounded needs the next power of two (2, 4, 32, 32). Public bound needs
   k = B̄ (2B here). It works whenever the competing background stays at or below B̄.
2. **Little prior knowledge is needed.** The attack needs only an upper bound on the competing max load, and over-injecting costs
   nothing except budget. Exact and rounded leakage provide this bound themselves: one earlier non-injected run gives advantage 1.000
   with the same k as full knowledge. The public-bound attack needs no data knowledge at all, because k = B̄ is public. A single
   same-size shadow sample of clustered real data underestimates C in 30–40% of splits (advantage 0.70). A 2× margin restores 1.000.
   The only failing setting is a badly misspecified prior on clustered data. Assuming uniform data on code or CIFAR gives k = 4–5 against
   C ≈ 15 and advantage 0.
3. **The DP release meets its guarantee, and the attack attains it.** Measured 0.458–0.465 (ε=1) and 0.047–0.054 (ε=0.1)
   against the optimum tanh(ε/2) = 0.462 / 0.050. Every knowledge setting reaches the same values once k ≥ C + 1, so beyond that point
   knowledge affects only the budget k. Advantage drops only when k < C. The single-shadow rows on real data reach 0.32 and 0.035
   for that reason. Prop. `prop:dp`'s e^ε − 1 + δ is valid but loose and is vacuous at ε=1. Suggest (e^ε − 1 + 2δ_dp)/(e^ε + 1).
4. **Cost of protection** (Table D). Rounded costs ≤1.29× and public bound costs B̄/B. Neither protects against injection. DP at ε=1 costs
   1.7–1.8× on the real datasets and 8–20× on uniform data. At ε=0.1 it costs 8–9× real and 68–190× uniform.

## Notes for the paper text

- protocol.tex says the DP mode "costs about (s+1/ε)ΣV extra probes per vector". The measured extra is E[max(0,s+Z)]·ΣV,
  which is s to within 0.01 (21.0 and 208.0) because Z is symmetric. (s + 1/ε) is a mild overestimate (22 and 218). Either keep it as an upper
  bound or say "≈ sΣV".
- security.tex: "advantage at most e^ε − 1 + δ_dp". Replace it with, or add, the tight form above. Our attack reaches it, so the bound is tight.
- The example in security.tex (k = 4 = power of two = B̄) is consistent with the measured rule. For general data, k must exceed
  the competing max C, which is 26–29 on the real datasets (Table A, column C).

## Limitations (to be stated honestly)

- **Chosen-fingerprint injection.** We inject fingerprints directly with chosen key bits. Whether a content-only adversary can
  do this was **not measured**. Two caveats weaken the paper's sentence "may be hard when only content can be uploaded". First, the
  fingerprint functions are public, and only the |S_t| = 16–22 key bits must be hit. Random content search therefore needs about
  2^{|S_t|} hash evaluations per injected vector, roughly k·2^16 ≈ 2·10^6 for code and k·2^22 ≈ 10^8 for CIFAR. Second,
  dHash bits are signs of adjacent-pixel differences, so an image with a prescribed dHash can be constructed directly (modulo the
  ingestion pipeline's resize and recompression). We recommend not relying on the content barrier.
- **One predicate per run.** B reveals only the maximum bucket, so each run tests one (t, z). A larger budget does not test more
  targets in parallel under exact or rounded leakage.
- **Target restriction.** The predicate differs between hypotheses only when the target is alone in its bucket. That holds for 78–100%
  of (y, t) pairs, and we sample targets only from those pairs.
- **Replacement vector.** For b = 0 it is drawn from the sender-side set of the same corpus (real data) or uniformly (synthetic).
  It can raise a competing bucket by one. This is why k = C + 1 rather than C is the safe choice (code: k = C gives 0.983).
- **Shadow rows for real data use half of Y as the receiver.** Bounds use a fixed B̄ = 2B. DP results are for a single run.
  The prior-run setting spends a second ε, so the composed guarantee is 2ε, although our decision uses only the second run.
- The receiver-side U leakage (sender multiplicity) is symmetric and was not evaluated here.
