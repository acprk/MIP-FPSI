# E6 — candidate statistics of the public block cover (plaintext analysis)

Date 2026-11-12 (PDT). No protocol run; nothing timed.

## Method
- Tool: `cand_stats.cpp` (built in `build/`, linked against the unchanged `impl/build12/libmipfpsi.a`).
  The tool calls the library's own `MipParams::make(d, delta, b, s, publicSeed)` and `uniqueRows`, so
  tables, key lengths and radius are exactly those of fpsi_bench/net_bench. Run with `run_e6.sh`.
- Permutation seed: there is a single public seed in the code, `FpsiConfig::publicSeed =
  block(0x5eed, 0x1234)` (fpsi.h:34). No script or data file overrides it (grep over impl/src and
  impl/scripts), so all paper runs used it; it is used here too.
- Data:
  - (i), (ii) synthetic: the exact generator of fpsi_bench/net_bench (`-seed 1`, X and Y uniform,
    `min(m,n)/8 = 512` planted near-copies at distance 0..delta+2), m = n = 2^12, d = 128. Same inputs as the
    paper's `-m 4096 -n 4096 -d 128` runs.
  - (iii) `data/code_simhash64.bin` (CPython 3.11.9 vs 3.12.4 SimHash-64), cover (4,1), delta = 3, as in
    run_campaign3/5 (`-b 4 -s 1`).
  - (iv) `data/cifar_dhash64_mild.bin` (the file whose json gives 97.5% genuine pairs within delta = 4, as stated in
    eval.tex), cover (6,2), as in run_campaign3/5 (`-b 6 -s 2`).
  - X and Y are deduplicated as in the protocol; the resulting sizes match the paper (CPython 2,973/2,959,
    CIFAR 9,999/54,994) and so do the output sizes |I| (CPython 2,265, CIFAR 4,923).
- Definitions. Key of table t = the bits of Y (or X) on the positions of table t. B = max bucket load over
  all tables. Q = B·Σ_t V(ℓ_t, r) probes per sender vector (protocol slots). "Non-empty probed buckets"
  = number of (t, z), z in the radius-r ball around x's key of table t, whose receiver bucket is non-empty.
  A candidate pair (x, y) = y lies in at least one probed bucket of x (distinct pairs, before padding to
  B). True = H(x, y) ≤ δ. Completeness check: brute force over all m·n pairs. Uniform estimate =
  Σ_t V(ℓ_t, r)/2^ℓ_t · m·n (expected number of (pair, table) key coincidences for uniform data; an upper
  bound on distinct candidates). Bucket-load statistics are over all non-empty buckets of all tables pooled.

## Results (`cand_stats.csv`, histograms `hist_*.csv`)

| case | T | ℓ_t | r | ΣV | B | Q | m·Q slots | non-empty probed / x (mean, max) | candidate pairs | true | false (of which δ<H≤2δ) | brute-force true pairs | uniform estimate |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| (i) synth d128 δ4 (5,1) | 5 | 25×2, 26×3 | 0 | 5 | 2 | 10 | 40,960 | 0.33, 5 | 472 | 363 | 109 (107) | 363 | 1.75 |
| (ii) synth d128 δ16 (9,1) | 9 | 14×7, 15×2 | 1 | 137 | 5 | 685 | 2,805,760 | 27.6, 47 | 124,187 | 457 | 123,730 (15) | 457 | 123,904 |
| (iii) CPython SimHash-64 δ3 (4,1) | 4 | 16×4 | 0 | 4 | 33 | 132 | 392,436 | 3.01, 4 | 11,767 | 3,267 | 8,500 (2,492) | 3,267 | 537 |
| (iv) CIFAR dHash-64 δ4 (6,2) | 15 | 20×1, 21×8, 22×6 | 0 | 15 | 12 | 180 | 1,799,820 | 6.01, 15 | 16,872 | 4,984 | 11,888 (350) | 4,984 | 3,409 |

Bucket loads (non-empty buckets, all tables pooled):

| case | non-empty buckets | max | p99 | p90 | mean |
|---|---|---|---|---|---|
| (i) | 20,448 | 2 | 1 | 1 | 1.0013 |
| (ii) | 32,994 | 5 | 2 | 2 | 1.117 |
| (iii) | 10,229 | 33 | 4 | 1 | 1.157 |
| (iv) | 793,937 | 12 | 2 | 1 | 1.039 |

## Observations
- Completeness: in all four cases every true pair (H ≤ δ) shares a probed key (true candidates = brute-force
  count), as the cover guarantees.
- (i) Uniform-like data: the 109 false candidates are almost all (107) planted near-copies at distance
  δ+1, δ+2 (the generator plants distances up to δ+2); the uniform estimate of 1.75 background pairs fits.
  The protocol evaluates m·Q = 40,960 slots for 472 candidate pairs (B = 2 doubles Q).
- (ii) δ/d = 1/8: the uniform estimate (123,904) predicts the 124,187 candidates within 0.2%; 99.6% of
  candidates are false and unrelated (H > 2δ). Q = 685 slots per vector against ~30 candidates per vector.
- (iii) CPython: clustering raises candidates 22× above the uniform estimate (11,767 vs 537), and the
  max load B = 33 is set by a single bucket (p99 = 4, p90 = 1). Q = 4·33 = 132 slots per vector against
  ≈ 4 candidates per vector on average: the B-padding, not the candidate count, drives the cost.
- (iv) CIFAR: 4.9× above the uniform estimate; B = 12 from one bucket (p99 = 2); 180 slots per vector
  against 1.7 candidates per vector on average.
