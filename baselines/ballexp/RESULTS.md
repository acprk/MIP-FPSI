# Baseline: receiver-side Hamming-ball expansion + plain PSI (volePSI RR22)

Date: 2026-10-23 (PDT). Host loaded by other jobs (load1 ≈ 18–22, 32-core cgroup quota). Per the
coordinator, the primary metric is **CPU seconds per party** (getrusage user+sys); wall time is
secondary.

## What it is

R holds Y (n vectors, d bits); S holds X (m vectors). R must learn I = {x ∈ X : ∃y ∈ Y, HD(x,y) ≤ δ}.
R expands each y into its radius-δ Hamming ball, takes the union, pads it with random dummies to
exactly N_R = n·V(d,δ) items (only m and n·V leak), and runs plain PSI with X.

* PSI: **unmodified upstream volePSI** (`third_party/volepsi`, commit ec76012, built in `out/build`;
  libOTe/cryptoTools/coproto/macoro from `<VOLEPSI_ROOT>/out/install/linux`,
  clean upstream). Protocol `RsPsiSender` / `RsPsiReceiver` (RR22 VOLE+OKVS PSI), semi-honest,
  ssp = 40, default libOTe VOLE mult type, `numThreads = 1`.
* **Orientation is correct, no workaround needed.** In RsPsi the *receiver* learns the intersection
  (`mIntersection` = indices into its own input), and the receiver's set size can be arbitrary
  (`init(senderSize, recverSize, …)`). So R (the big expanded set, N_R items) = `RsPsiReceiver`, and S
  (m items) = `RsPsiSender`, which learns nothing. R maps indices back to its items. Since the items in
  the ball are exactly the candidate x values, those are the elements of I.
* Cost of this orientation: R encodes its whole N_R-item set into an OKVS and sends it, so
  **R→S traffic is linear in n·V (≈ 21.87 B per expanded item)**. S→R is tiny: m masks of
  ⌈(40 + log2(m·N_R))/8⌉ = 10 bytes plus OT/VOLE setup. RR22 has no variant that ships only O(m) here
  while the big-set holder learns the output.
* Measured cost depends only on (m, N_R). Timed runs use synthetic random items of exactly those sizes
  (R item i = AES_k(0,i); S plants m/8 of them, the rest come from a disjoint domain). The planted
  count comes back exactly as the intersection size in every run, which is a sanity check.

Driver: `src/ballexp_bench.cpp` (two processes, loopback TCP via `coproto::asioConnect`, like
`impl/src/net_bench.cpp`). Build:
```
cmake -S baselines/ballexp -B baselines/ballexp/build -DCMAKE_CXX_COMPILER=/usr/bin/c++ -DCMAKE_BUILD_TYPE=Release
cmake --build baselines/ballexp/build -j 2
```
Runner: `run_pair.sh <label> <coreS> <coreR> <port> <args>` (taskset + `/usr/bin/time -v`). The timed
series is in `timed_campaign.sh` (S on core 40, R on core 41; waits for load1 < 30 and enough
MemAvailable; logs load and the change in `nr_throttled` / `throttled_usec` of user-1000.slice).
Raw logs are in `logs/`.

Metric definitions: `psi_*` covers connect through flush and excludes data generation. CPU = getrusage
user+sys of that process over the protocol. MiB = coproto socket byte counters (2^20 B). Peak RSS =
`/usr/bin/time -v` "Maximum resident set size" for the whole process.

## Correctness check (real expansion, brute force), m = n = 256, d = 64, δ = 3

`run_pair.sh correct_real_256 46 47 1411 -mode real -m 256 -n 256 -d 64 -delta 3 -seed 7`

Random X and Y, with 64 planted pairs at distances 0..5 (cycled). R really expands the 256 balls,
sorts and deduplicates, and pads with dummies. For d < 128 each dummy has a bit set above position d,
so a dummy can never equal a real item. Then R shuffles and runs RsPsi. The output is compared with a
brute-force O(mn) Hamming scan.

| union before padding | padded to n·V | got | brute-force truth | false neg | false pos |
|---|---|---|---|---|---|
| 11,196,800 | 11,198,720 | 37 | 37 | 0 | 0 → **PASS** |

This run was not a timing run: it used unpinned spare cores 46/47 because 40/41 were busy at that
point. R's expansion, sort and padding took 4.63 CPU-s (13.1 s wall). The PSI part had the same
traffic as the synthetic m=n=256 run below (244,899,269 B R→S), which confirms that cost does not
depend on the data.

## Timed runs (one run each; S pinned to core 40, R pinned to core 41)

| run | m | n | N_R = n·V | R CPU s | S CPU s | wall s | R→S MiB | S→R MiB | R peak RSS | S peak RSS | load1 before | Δnr_throttled (Δthrottled s) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| synth m=n=256, d64 δ3 | 256 | 256 | 11,198,720 | 9.02 | 2.84 | 10.5 | 233.55 | 0.159 | 2.49 GiB | 0.70 GiB | 18.83 | 0 (0) |
| **CPython size**, d64 δ3 | 2973 | 2959 | 129,441,455 | **164.08** | **65.59** | **200.1** | **2693.24** | 0.215 | **25.1 GiB** | 8.0 GiB | 18.79 | 16 (34.2) |
| synth m=n=2^12, d64 δ3 | 4096 | 4096 | 179,179,520 | **262.60** | **69.58** | **315.6** | **3736.93** | 0.226 | **39.8 GiB** | 11.1 GiB | 22.03 | 29 (60.4) |

Exact bytes: CPython R→S 2,824,062,133 / S→R 225,631; 2^12 R→S 3,918,456,661 / S→R 236,861.
R prep for synthetic data (excluded): 1.36 s (CPython). Wall time is R's `psi_wall_s`. S's wall is
almost the same, but S is mostly idle waiting for R (its CPU is about 1/3 of wall).

Per-item constants from the two large runs: R→S ≈ 21.87 B/item. R CPU ≈ 1.27–1.47 µs/item. S CPU ≈
0.39–0.51 µs/item. R RSS ≈ 208–238 B/item. S RSS ≈ 67 B/item.

## Skipped (over the 60 GB limit). Extrapolated from the per-item constants above

V(64,3) = 43,745. V(128,4) = 1 + 128 + 8,128 + 341,376 + 10,668,000 = 11,017,633.

| setting | N_R | R→S traffic | R peak RSS (est.) | R CPU (est., 1 thread) | run? |
|---|---|---|---|---|---|
| d64 δ3, m=n=2^14 | 716,718,080 | ≈ 14.6 GiB | ≈ 149–171 GB (+ S ≈ 48 GB) | ≈ 910–1,050 s | skipped (> 60 GB) |
| d128 δ4, n=2^8 | 2,820,514,048 | ≈ 57.4 GiB | ≈ 590–670 GB | ≈ 3,600–4,100 s | skipped |
| d128 δ4, n=2^12 | 45,128,224,768 | ≈ 919 GiB | ≈ 9.4–10.8 TB | ≈ 57,000–66,000 s | skipped |

For d128 the expanded set has 2.8e9 items at n=2^8 and 4.5e10 items at n=2^12. The second is above
2^32, and I did not check whether volePSI/Paxos index types even support it. With R holding all of
it, at d=128 this baseline cannot run on one machine except at very small n.

## Caveats

* Host was loaded (load1 18–22) and the user cgroup was throttled during the two large runs: 34 s and
  60 s of throttled time across the slice. That inflates wall time (R wall/CPU ≈ 1.2) and possibly
  CPU through cache and memory contention. Cores 40/41 had no other running threads at the start of
  the timed runs (logged in `logs/*.env`). Core 40 had had a foreign python3 thread a few minutes
  earlier. One run per setting.
* Single-threaded (`nt=1`). volePSI's multi-threaded mode would cut wall time but not total CPU or
  communication.
* Loopback TCP, no bandwidth limit. At 1 Gbit/s, the 2.63 GiB of the CPython-size run adds ≥ 22.6 s
  of transfer; at 100 Mbit/s it adds ≥ 226 s.
* Synthetic timings exclude R's ball expansion, deduplication and padding. In real mode at n=256 these
  took 4.6 CPU-s. Linear-ish scaling (plus sort log-factor) puts them at roughly 55–65 CPU-s at the
  CPython size; that figure is an estimate, not a measurement. The padded size is fixed at n·V, so
  overlap between balls never changes the PSI cost.
* Security: semi-honest RR22, λ = 128, ssp = 40. Leakage is only m and n·V. The baseline needs no
  assumption on the data distribution.
