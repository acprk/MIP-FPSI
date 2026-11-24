# Round-3 protocol modes (build5): U-free and DP-overestimated loads

Built in `build5` (CXX=/usr/bin/c++). build3/build4 untouched. New/changed sources:
`src/oep.{h,cpp}` (new), `src/fpsi.{h,cpp}`, `src/mip.{h,cpp}` (senderMap noCopy flag),
`src/main.cpp`, `src/net_bench.cpp` (flags), `src/unit_tests.cpp`, `CMakeLists.txt` (oep.cpp),
`scripts/validate_modes_r3.sh`.

Flags (fpsi_bench and net_bench): `-noU`, `-dpeps <eps>`, `-dpdelta <delta>` (default 2^-30);
combinable with `-pow2` (DP first, then rounding) and with each other.

## Mode 1: U-free (`cfg.noU`)

Goal: the receiver learns only m. The sender multiplicity U and the receiver program size
n*T*U disappear (R programs n*T keys).

1. S computes the base probe keys K(t,z,c) of all M = m*Q slots (slot i*Q+k, unchanged order),
   deduplicates them, and pads the distinct list with uniformly random dummy blocks to exactly M
   items. The cuckoo size N therefore depends only on (m, Q), i.e. on (m, B) and public values.
2. Payload sharing (unchanged HamCpsi code): R programs its n*T base keys with payload = y
   restricted to the complement of table t (zero offset/tag field). Both get XOR shares of a
   payload per cuckoo bin.
3. Oblivious extended permutation (Mohassel–Sadeghian EC'13), `src/oep.cpp`: Beneš permutation
   -> replication layer (out_j = bit_j ? out_{j-1} : in_j) -> Beneš permutation on
   P = 2^ceil(log2 max(N, M)) wires, over w = pb-byte words. S is the programmer (map: slot ->
   bin of its distinct item; duplicates allowed); R is the data holder of its bin shares.
   Evaluation as in osn.cpp: R masks every wire with a random w-byte mask; per gate one silent
   random OT on S's control bit (derandomised by e = c xor bit); R sends both corrections
   encrypted under fixed-key-AES (correlation-robust hash) expansions of the OT keys; S
   evaluates locally. S adds its own bin share of map[s] locally.
4. S XORs its x-dependent bits (complement gather + offset ones) into its slot share; the
   threshold GMW circuit runs over the M slots (not N bins); the OR circuit runs over the Q
   consecutive slots of each element; delivery unchanged. The old 1-bit OSN step is gone.

Leakage: L_S = (n, B) [or ceil(log2 B) with -pow2, or B + DP noise], L_R = m.
Topology of the extended-permutation network depends only on (N, M), both functions of
(m, Q) i.e. of (m, n, B) and public parameters; control bits are only used inside OT.

Fallback rule in noU mode: same decision function `preferAllPairs`, but fed the slot count
M = m*Q instead of N for the threshold/switching size (both parties compute it identically).

Communication (exact, load-independent), d=128, delta=4, cover (5,1), m=n=2^12, B=2, Q=10:

| mode | comm total | S->R | R->S | programmed keys |
|---|---|---|---|---|
| default | 9.98 MiB | 5.08 | 4.90 | 40,950 (= n*T*U, U=2) |
| -noU | 110.66 MiB | 4.61 | 106.06 | 20,475 (= n*T) |

The R->S traffic is the extended-permutation corrections: about
2*[2 networks * (P/2)(2 log2 P - 1) * 2w + P*w] bytes, P = 65,536, w = 13 B -> ~107 MB.
So U-freeness costs ~11x communication at d=128 (still ~21x less than otFPSI's 2,322 MiB);
CPU time in a loaded single-process run was 3.90 s vs 3.27 s (not a result; host loaded).
At d=64 with tag bits (tau=26) the words are wider relative to the default 1-bit OSN:
CPython (-noU) 793 MiB vs 66 MiB default.

## Mode 2: DP-overestimated loads (`cfg.dpEps > 0`)

R reveals B~ = B + eta, eta = max(0, s + Z), Z = G1 - G2 with G_i ~ Geometric(1 - e^-eps)
(P(Z=z) ~ e^{-eps|z|}), s = ceil(ln(1/dpDelta)/eps); S does the same for U unless -noU.
Randomness: the party's AES-CTR PRNG. Overestimation only, so correctness is unaffected (the
protocol already handles any revealed B >= true B, as with public bounds). Neighbouring inputs
(one vector added/removed) change B (and U) by at most 1, so the revealed value is
(eps, dpDelta)-DP with respect to one vector; k injected vectors give (k*eps, ~k*dpDelta) by group
privacy. Applied before -pow2 rounding (post-processing). If a public bound Bbar/Ubar is set it
takes precedence (no noise).

Cost: with eps = 1, dpDelta = 2^-30, s = 21, so B~ is about B + 21 (observed B~ = 19..26 for
true B = 1..5): Q grows ~10x at d=128, delta=4 (Q 10 -> 115 at 2^12; comm 9.98 -> 104 MiB).
Larger eps or dpDelta trade privacy for cost; the paper should present this as an option for
settings with input injection, not as the default.

## Tests (all pass)

`build5/unit_tests` (core 44):
- `oep (extended permutation, w in {1,13,20}): ok` — N in {5, 64, 1000, 4097}, M in {N, N/2, 1},
  skewed random maps with many duplicates; plaintext network and two-party protocol both checked
  (shares XOR to x[map[j]]; holder share not equal to plaintext).
- End-to-end -noU FPSI against brute force, m=n=300, (128,4,(5,1)) and (64,3,(4,1)), uniform and
  clustered (6 clusters; default-mode U would have been 10-16), each with and without -dpeps 1:
  all exact, info.U = 1, info.programmed = n*T.
- Existing tests (Beneš/OSN, covers, circuits, radius, MIP no-FN) unchanged and passing.

`scripts/validate_modes_r3.sh` (fpsi_bench, cores 44-47; results in
`../results/modes_r3_20261023_*/summary.txt`), every run false_neg=0 false_pos=0:

| config | -noU | -dpeps 1 | both |
|---|---|---|---|
| (128,4,(5,1)) 2^10 | 84/84 | 84/84 | 84/84 |
| (128,4,(5,1)) 2^12 | 352/352 | 352/352 | 352/352 |
| (128,8,(9,1)) 2^10 | 98/98 | 98/98 | 98/98 |
| (128,8,(9,1)) 2^12 | 399/399 | 399/399 | 399/399 (after chunking fix) |
| CPython (64,3,(4,1)) | 2265/2265 | 2265/2265 | 2265/2265 |

Communication of these runs (MiB): CPython default 66.0, -noU 793, -dpeps 1 108.6, both 1,653;
(128,8) 2^12 -noU 564, -dpeps 1 236, both 5,143.

Bug found and fixed during validation: the first both-modes (128,8) 2^12 run hung, because the
extended-permutation correction message (~4.5 GB at P = 2^21) exceeded coproto's 32-bit message
length. `oep.cpp` now sends/receives large buffers in chunks of 256 MiB; the rerun passed and the
unit tests were re-run on the fixed binary.

Two-process TCP (`net_bench -noU`, m=n=2^10, (128,4), cores 44/45): matches 84/84, FN=FP=0,
U=1 reported by both roles.

## Limitations

- U-free mode costs ~11x communication (d=128) because the extended permutation moves w-byte
  payload shares instead of 1-bit results; memory grows with P*log P*w (5 GB RSS at
  P = 2^21, w = 13 in the both-modes delta=8 run).
- DP mode protects against injection only up to group privacy (k*eps for k injected vectors);
  the attack in the privacy review injects ~B vectors, so eps must be small for strong
  protection, which raises the additive overhead s ~ ln(1/delta)/eps.
- The fallback cost model is unchanged in form (communication-oriented); in noU mode it does not
  account for the larger extended-permutation words.
- Times above are from a loaded shared host and are not results.
