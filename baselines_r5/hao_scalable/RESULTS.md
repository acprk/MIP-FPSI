# Hao et al., "Towards Scalable Fuzzy PSI via Efficient Fuzzy Matching" (CCS'26, arXiv 2608.11526) — is it a Hamming baseline?

Date: 2026-11-11 (PST). Workspace: `baselines_r5/hao_scalable/` only. Nothing outside it was modified.

## Verdict

**It is not a comparable exact-Hamming FPSI baseline.** Its high-dimensional `L_p` protocol is correct only if **both** parties' sets satisfy the *globally disjoint* assumption (Def. 7.1). On binary vectors that assumption cannot hold for any set of ≥ 2 points when δ ≥ 1. With binary inputs the artifact crashed in every case we tried (6 of 6, including our CPython data). It produced no output, so there are no FN/FP counts to report.
On its own synthetic, globally disjoint data at d = 128 (where it does run), it costs 7.3× (δ = 4) and 3.1× (δ = 8) more CPU than our exact protocol at 2^12 on the same host. It also sends 19× / 7.4× more data and uses 19× / 7.4× more memory.

## 1. What the paper claims (read from the arXiv PDF, `2608.11526.pdf`, `paper.txt`)

| Item | Statement in the paper |
|---|---|
| Functionality | F_FPSI (Fig. 2): S holds Q = {q_j}_{j∈[m]} ⊂ Z_{2^ℓ}^d and R holds W = {w_i}_{i∈[n]}. R learns Z = {q_j : ∃ i, dist(q_j, w_i) ≤ δ}, i.e. the actual sender points. The metric is L_∞, L_1 or L_2. |
| Security | Semi-honest. Proofs are in the (F_OPPRF, F_FMatch, F_OT)-hybrid model (Thm 7.3 for L_p high-dim; Thm 7.2 for L_∞). κ = 128, λ = 40. |
| High-dim assumption | Thm 7.2/7.3 hold "**if both parties' sets satisfy the globally disjoint assumption**". Def. 7.1: a set of radius-δ balls with centres q_1..q_m is globally disjoint if for **every** dimension k, the projections [q_{j,k} − δ, q_{j,k} + δ] are pairwise disjoint over j. That is, |q_{j,k} − q_{j',k}| > 2δ for all j ≠ j' and **all** k. The paper itself says this "enforces a strong structural constraint and is not entirely realistic" (§7). The table notation is "R ∧ S, global disj.". |
| Why it is needed | App. C: the OPPRF cannot program the same key (k, prefix) to two different values (sender side). Two receiver queries in the same interval would also leak per-dimension partial matches (receiver side). The L_p protocol (Fig. 14) builds identifiers id_q, id_w from the L_∞ protocol. These identifiers collide iff dist_∞ ≤ δ, and only the colliding pairs get the OT-based L_p check. |
| FN / FP | The paper states no separate FN/FP bound. Correctness (App. E) holds except with probability negligible in λ, given the assumption. Outside the assumption it guarantees nothing. |
| Complexity (high-dim L_p, Table 1) | Communication O((m + n·p)·d·log δ). Sender computation O((m + n·p)·d·log δ). Receiver computation O(n·p·d·log δ + m·d). That is linear in d, logarithmic in δ, and has no 2^d factor. In the implementation, log δ is rounded up to multiples of 8, so measured cost barely changes with δ. |
| Hamming | Footnote 1: Hamming FPSI "[3, 8, 10, 15, 27, 44, 49] … we do not discuss in detail here". All experiments use synthetic integer data with d ∈ {16, 32, 64} and δ ∈ {16 … 1024}. |

### Why binary Hamming data can never satisfy the assumption
L_1 on {0,1}^d is the Hamming distance, so the L_1 mode *could* be applied to binary data in principle. But every coordinate of a binary vector is 0 or 1, so |q_{j,k} − q_{j',k}| ≤ 1 ≤ 2δ for every pair and every k whenever δ ≥ 1. Every pair of balls therefore overlaps in **every** dimension. The assumption fails for any set of ≥ 2 points. For n ≥ 3, the pigeonhole principle also gives exact duplicate OPPRF keys (k, prefix) in every coordinate, on both sides. This does not depend on the data distribution: uniform random data fails just as much as clustered data. Any coordinate-wise integer embedding that keeps L_1 = Hamming has the same problem.

## 2. Build (g++-12, C++20)

Upstream: `https://github.com/Th0masAndy/Scalable-FPSI`, codeload tarball of HEAD = commit `1a9d3495f4d5fd9c6dd04f4d6e5cc8832d750bb2` (sha256 of tarball `63dd2ce9…`). Dependencies: volePSI `59e06bc` (as pinned by upstream `build.sh`) and BLAKE3 `c7f0d21`. Script: `build_deps.sh`, then `cmake … -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 -DBoost_DIR=boostprefix/lib/cmake/Boost-1.86.0` and `cmake --build build -j4`. Total time about 1 h 40 min, including two dead ends. Binary: `Scalable-FPSI-HEAD/build/fpsi` (sha256 `a3aedb9d…`), built with `-O3 -march=native`.

| # | Patch (`patches/`) | Why |
|---|---|---|
| 01 | `01-volepsi-libote-pin-and-no-boost-fetch.patch` (volePSI `thirdparty/getLibOTe.cmake`) | (a) **Upstream HEAD does not link.** volePSI `59e06bc` pins libOTe `36cd724` (Dec 2024), which has no `libOTe/Triple/SilentOtTriple`, but `cmp.cpp`/`and.cpp` use it. An earlier upstream `build.sh` (seen in an older checkout on this host) did a `sed` to libOTe `d21bc4d`. We apply the same pin. (b) `-DFETCH_BOOST=OFF`, so the system Boost 1.86 is used through a symlink-only prefix (`boostprefix/`) instead of a ~130 MB download. That download also failed with a hash mismatch. |
| 02 | `02-fpsiHighPx-localasync-fmt-fileio.patch` | (a) Transport: upstream uses `coproto::AsioSocket::makePair()` = TCP `localhost:1212`, which is blocked here. Replaced by `coproto::LocalAsyncSocket::makePair()`: in-process, same two-thread structure, and the byte counters still work. (b) `std::format` → `fmt::format` (g++-12 lacks `<format>`). (c) Optional `-in DIR` (binary vectors from files, coordinate = 2^20 + bit) and `-out FILE` (dump of R's output plus id-collision diagnostics). These are used only for the correctness tests. Without `-in`, the upstream generator runs unchanged. |
| 03 | `03-fpsiLowPx-fmt.patch` | `std::format` → `fmt::format` (compile only). |
| 04 | `04-oprf_impl-uaf-ref-send.patch` | With LocalAsyncSocket, every high-dim run with d ≥ 16 segfaulted. ASan found a heap-use-after-free: in `LargeOprfReceiver::receive`, a moved `SharedSlice` send was released before the peer copied it (`logs/asan_d16.log`). The fix is a one-line by-reference send of the same bytes, which completes only after the bytes are consumed. Nothing on the wire changes. After the fix, ASan reports no errors, only leaks (`logs/asan_d16_b.log`). With TCP the bug may not show, because bytes leave the buffer at write time. |

## 3. Correctness on binary data (task 5)

Data from `scratch/gen_data.py` → `data/<case>/` (sender.txt, receiver.txt, expected.txt = brute-force Hamming truth, stats.json). Checker: `scratch/check_correct.py` → `runs/correctness.json`.

| Case | d | δ | m = n | true matches | receiver pairs ≤ 2δ | senders with ≥ 2 receivers ≤ δ | Hao result |
|---|---|---|---|---|---|---|---|
| rand_d128_t4_2e8 (n/8 planted copies, 0..δ+2 flips) | 128 | 4 | 256 | 24 | 0 | 0 | **abort** (rc 134): volePSI `PaxosImpl.h:2829` OKVS bin overflow |
| rand_d128_t4_2e10 | 128 | 4 | 1024 | 87 | 0 | 0 | **crash** (rc 139). gdb on a rerun: same Paxos throw, in R's first OPPRF |
| rand_d128_t8_2e8 | 128 | 8 | 256 | 31 | 0 | 0 | **crash** (rc 139) |
| rand_d128_t8_2e10 | 128 | 8 | 1024 | 101 | 0 | 0 | **abort** (rc 134) |
| clustered_d128_t4_2e10 (+64 receivers within 4 flips of one centre) | 128 | 4 | 1024 | 115 | 2016 | 23 | **abort** (rc 134, `munmap_chunk(): invalid pointer`) |
| cpython_d64_t4 (our CPython SimHash64, dedup → 2959 each) | 64 | 4 | 2959 | 2342 | 3556 | 241 | **crash** (rc 139) |
| control: upstream synthetic generator (globally disjoint) | 128 | 4 | 1024 | 10 planted | – | – | runs. 10 id collisions → 10 results = the 10 planted points |

So FN = FP = undefined: **no output in 6/6 binary cases**. The failure is structural, as §1 predicts. Duplicate (coordinate, prefix) keys with conflicting values make the OKVS (Baxos) encoding fail or corrupt memory, whether or not the data is clustered. Even uniform random binary data violates the assumption. Note on the upstream L_1 benchmark generator: it plants only log2(n) matches. For d = 128 and δ = 4, the per-coordinate noise is floor(δ/d) = 0, so the planted points are **exact copies**. The near-match logic of the L_1 path is never exercised in their high-dim L_1 benchmark at these parameters.

## 4. Cost on the same host (task 4/6), upstream synthetic data

Commands (`scratch/runner.py bench`; one run at a time; `/usr/bin/time -v taskset -c 40,41 …`):
- Hao: `fpsi -high -p 1 -nn {8,10,12} -d 128 -delta {4,8} -try 1` (2 threads = S and R, single-threaded OPPRF/OT, LocalAsyncSocket).
- Ours: `baselines_r5/gao_ext/build_ours/fpsi_bench -m n -n n -d 128 -delta δ -b δ+1 -s 1 -nt 1 -indexverify -seed <crc32(tag)>` (in-process, single thread; same library as `impl/build11`, see `../gao_ext/RESULTS.md`). Every run was verified against the plaintext index: FN = FP = 0.

CPU is user+sys of the whole process (both parties). Comm is MiB in both directions (Hao: socket byte counters; ours: `comm_MB` = bytes/2^20). Values are median [min–max] (3 reps at 2^12, 1 otherwise). Runs were interleaved (Hao, ours, Hao, …), 15:19–15:24 PST.

| Protocol | δ | m=n | CPU s | wall s | Comm MiB | max RSS MB | nr_throttled Δ |
|---|---|---|---|---|---|---|---|
| Hao high-dim L1 | 4 | 2^8 | 2.14 | 2.15 | 16.6 | 331 | 0 |
| Ours exact | 4 | 2^8 | 0.32 | 0.33 | 1.2 | 28 | 0 |
| Hao high-dim L1 | 4 | 2^10 | 7.63 | 7.63 | 57.5 | 1 222 | 0 |
| Ours exact | 4 | 2^10 | 0.85 | 0.86 | 2.7 | 68 | 0 |
| Hao high-dim L1 | 4 | 2^12 | **31.65** [31.51–31.69] | 30.79 | **222.3** | **4 719** | 3,0,0 |
| Ours exact | 4 | 2^12 | **4.36** [4.14–4.41] | 4.36 | **11.5** [11.5–12.6] | **244** | 0,0,0 |
| Hao high-dim L1 | 8 | 2^8 | 2.37 | 2.26 | 18.7 | 327 | 0 |
| Ours exact | 8 | 2^8 | 0.55 | 0.56 | 2.0 | 54 | 0 |
| Hao high-dim L1 | 8 | 2^10 | 7.82 | 7.67 | 65.7 | 1 185 | 0 |
| Ours exact | 8 | 2^10 | 2.17 | 2.18 | 7.2 | 100 | 0 |
| Hao high-dim L1 | 8 | 2^12 | **32.15** [31.83–33.08] | 31.92 | **255.3** | **4 744** | 0,4,2 |
| Ours exact | 8 | 2^12 | **10.34** [10.17–10.73] | 10.34 | **34.6** [34.6–36.7] | **643** [572–643] | 1,2,0 |

Ratios Hao/ours at 2^12: δ = 4: CPU 7.3×, comm 19×, RSS 19×. δ = 8: CPU 3.1×, comm 7.4×, RSS 7.4×. Against the paper's Table IV value for ours (3.2 CPU-s, δ = 4, 2^12, an earlier campaign on other cores), the δ = 4 CPU ratio would be 9.9×. Our same-session number (4.36) is the paired one, and it matches `../gao_ext` (4.36). Sanity check against the paper: Table 6 reports 9.49 s and 146 MB for d = 64, δ = 16, 2^12 on an EPYC 9554. Our d = 128 numbers are consistent with that, given 2× d and a slower CPU.

## 5. Environment and caveats

- Host: 2× Xeon Gold 6230R (52 cores / 104 threads), Linux 6.8.0-138, g++ 12.3.0, CMake 4.4.3. The host is shared: load average was 12–22 during the runs. Leftover check before every run: `ps -C fpsi,fpsi_bench -o pid,psr,etime,args`. It never showed a leftover.
- **Contention:** another user's job (`0936project/ulf2pc`, launched with `-cores 40,41` / affinity 40-47) ran on our cores during part of the main session (seen at about 15:16 and 15:21 PST). CPU time was very stable across reps (Hao ±0.6 %, ours ±3 %), and the Hao and ours runs were interleaved, so the ratios should be robust. Wall times may still be slightly inflated. We prepared a clean repeat (`CLEAN=1 python3 scratch/runner.py bench_clean`). It runs each job only when no foreign user process with restricted affinity hits 40/41/92/93. Starting it was **blocked by the permission system**, so it was not run. The user can run it if wanted.
- Cost numbers for Hao are on **its own globally disjoint data**. On binary data it does not run at all, so these are lower bounds on what a Hamming deployment would cost, and they come with no correctness.
- Transport: LocalAsyncSocket, in process. Upstream used TCP over loopback with tc shaping (10 Gbps, 0.02 ms). Our numbers are CPU only, with no network time.
- Also present: `build_asan/` (debug and ASan build used to find the bug in patch 04), `logs/` (all build and ASan logs), `runs/` (per-run stdout, `time -v` output, `summary.jsonl`, `correctness.json`).

## Paths
- Paper: `2608.11526.pdf`, `paper.txt`. Code: `Scalable-FPSI.tar.gz`, `Scalable-FPSI-HEAD/` (patched), `patches/01–04`.
- Scripts: `build_deps.sh`, `scratch/gen_data.py`, `scratch/runner.py`, `scratch/check_correct.py`, `scratch/aggregate.py`.
- Data: `data/<case>/`. Results: `runs/summary.jsonl`, `runs/correctness.json`, `runs/*.out`, `runs/*.time`.
