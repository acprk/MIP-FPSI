# Blass–Noubir assumption-free Hamming FPSI: same-host single-thread baseline

Paper: E.-O. Blass, G. Noubir, "Assumption-Free Fuzzy PSI via Predicate Encryption", USENIX Security 2026, ePrint 2025/217.
Measured 2026-11-10/11 on the same host as our protocol and otFPSI.

## 1. Artifact

| Item | Value |
|---|---|
| Source | Zenodo 10.5281/zenodo.17936990, `https://zenodo.org/api/records/17936990/files/fuzzyPSICode.tar.xz/content` |
| File | `fuzzyPSICode.tar.xz`, 8 788 812 B |
| sha256 | `27558a2e16fa54979deb99936719f50028c79e4e70a1a4ef9e2e882596842441` |
| md5 | `0443782a64b7a91d2d45ea7175eb42be` (matches the Zenodo record) |
| License | CC-BY-4.0 |
| Extra downloads | coproto, macoro, function2 and bitpolymul git clones fetched by libOTe's CMake (small source repos). Nothing else was downloaded. |

## 2. Environment

- CPU: 2× Intel Xeon Gold 6230R @ 2.10 GHz (26 cores/socket, 2 threads/core, 104 logical CPUs). Cores 40 and 41 were used; their HT siblings are 92 and 93.
- OS: Linux 6.8.0-138-generic (Ubuntu 22.04 userland). The user-1000 cgroup has a CPU quota of 26 CPUs (`cpu.max 2600000 100000`).
- Compiler: g++ 11.4.0 (`/usr/bin/g++`, selected with `PATH=/usr/bin:/bin`; the default g++ 9.5 on PATH was not used). CMake 4.4.3.
- Project flags (unchanged from `src/CMakeLists.txt`): `-std=c++20 -O6 -fopenmp -march=native -maes -mrdseed`.
- Libraries: mcl as vendored (curve BN254, `op.hpp` version 0x212), GMP 6.2.1, OpenSSL 3.0.2, emp-tool/emp-ot as vendored (Ferret COT), libOTe/cryptoTools as vendored (only Blake2 is used).
- Binary sha256 values: `src/build/party` (unmodified protocol) `ece9ce52…772a`; `src_fix/build/party` (fixed + instrumented) `6041358e…6e3d`; `src_check/build/party` (instrumented only) `dfab8125…a597`.

## 3. Build

The build follows the authors' README:

1. mcl: `cmake -S mcl -B mcl/build -DCMAKE_BUILD_TYPE=Release`, then `make -j4 MCL_USE_OMP=1`. Under CMake, `MCL_USE_OMP` has no effect; it does not matter for single-thread runs.
2. emp-tool: `patch -p1 < patch.emp-toolkit` (the authors' patch), then `cmake . -DCMAKE_POLICY_VERSION_MINIMUM=3.5` and `make -j4`. emp-ot is header-only and was not built.
3. libOTe: the archive ships `libOTe/cryptoTools` as an empty submodule directory, so we copied the top-level `cryptoTools/` into it. Then: `cmake -S libOTe -B libOTe/out/build/linux -DCMAKE_BUILD_TYPE=Release -DFETCH_AUTO=ON -DSUDO_FETCH=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5` and `cmake --build … --parallel 4`. `liblibOTe.a` and `libcryptoTools.a` built. `libOTe_Tests` failed (missing `<cmath>`, `std::sqrt`), but the project does not need it.
4. Our code: `cmake -S src -B src/build` and `make -j4 party peiTest`. It compiled with no source changes.

SEAL, emp-sh2pc and MIRACL were not built because they are not used by `party`.

Total build effort was about 40 minutes, so the build is easier than "moderately hard".

### Patches (in `patches/`; pristine originals in `pristine/`)

| Patch | Why | Effect on timing |
|---|---|---|
| `01-emp-netio-fifo.patch` (emp-tool `net_io_channel.h`) | This host's sandbox blocks loopback TCP connections (`connect` to 127.0.0.1 times out) and blocks `AF_UNIX` sockets entirely. When `EMP_FIFO_DIR` is set, `NetIO` uses two named pipes per channel instead of a socket. Buffering (`NETWORK_BUFFER_SIZE`, full buffering) is the same. | Transport only. Total traffic is ≤ 43 MB per run and the receiver's reported "network" phase is ≤ 0.7 % of its time. This is effectively an unshaped loopback (no LAN/WAN shaping), the same as unshaped localhost TCP. |
| `02-party-count-successful-decryptions.patch` (`party.cpp`) | The code prints nothing checkable. We count `pei::decrypt()==true` in the receiver loop, overall and per τ. | One branch and counter increment per pairing product. The cost is not measurable: orig and fix match within 0.5 % at k=4. |
| `03-pei-sk1-ot-key-fix.patch` (`pei.cpp`) | **Bug in the artifact.** In `secretKeyOT::doSerializeAndEncrypt`, the sender masks the choice-1 key shares (SK1/κ1) with a PRG seeded from `keys_0` instead of `keys_1`. The receiver unmasks with its random-OT output, which is `keys_1` for choice bit 1. Every key component with v_l = +1 is therefore garbage, and **no decryption ever succeeds**: the unmodified protocol plus the counter gave 0/48 successes, where 16/48 were expected. We changed one token (`keys_0` → `keys_1`). | None. The work is identical: the same PRG expansion, and every `decrypt` performs the full multi-pairing (Miller loop over 4ℓ+2 pairs plus the final exponentiation) whether or not it succeeds. Measured: orig k=4 93.2 CPU-s vs fix 93.7; orig k=5 322.4 vs fix 336.2 (median of 3, which includes throttle noise). |

The headline numbers below use the **fix** build because it is the one that computes the correct result. The unmodified (`orig`) build is reported alongside for k=4 and k=5.

## 4. What is measured

- Binary semantics: `party logN_S logN_R ELL t {1=sender,2=receiver}`. The last of the ELL coordinates is the threshold slot (see `v[i][ELL-1]=1` and `realHDkder(…, ELL-2t)`), so the bit length is ELL−1. **We pass ELL = 129 for our d = 128.** The paper's "ℓ = 16/32" runs pass ELL = 16/32, i.e. 15/31 data bits. The authors' own `peiTest` usage line suggests ELL = 128, so large ELL is supported. The `uint32` `computeSize` limits the sender's key buffer to < 4 GiB; that buffer is about 0.64 MB per receiver element at ELL=129, t=5.
- Predicate HD < t. **t = 5 ⇔ our δ = 4; t = 9 ⇔ δ = 8.**
- Inputs are fixed by the code. Every sender and receiver element is the same alternating ±1 vector (HD = 0 for all pairs). The receiver tries all t keys of every receiver element against every sender ciphertext, so the cost does not depend on the data: n_S·n_R·t multi-pairings of 4·ELL+2 pairs each.
- Correctness check: with the fix, successes = n_S·n_R exactly, all at τ = 0 (HD = 0), and 0 at τ ≥ 1. This held for every fix run, e.g. 4096/20480 at k=6, t=5. The authors' primitive test `peiTest 16 2 3 C` passes all checks (`logs/peiTest_C_ell16_logn2_t3.txt`).
- Run protocol (`run_one.sh`):
  - `OMP_NUM_THREADS=1`. The sender runs under `taskset -c 40 /usr/bin/time -v`; the receiver runs concurrently under `taskset -c 41 /usr/bin/time -v`.
  - Wall = launch to both exits. CPU = user+sys of both processes.
  - Before and after each run the script records `ps -C party,run`, threads currently on cpus 40/41/92/93, `/proc/loadavg`, and the cgroup `nr_throttled`.
  - A quiet-host gate requires 0 throttle events in a 30 s window and load1 < 24 before starting.
  - A run with more than 100 throttle events per wall-minute is moved to `logs_excluded/` and retried, up to 3 attempts.
  - No foreign process was touched. The host was shared with about 14 long-running unpinned python jobs; their scheduler placement sometimes landed on 40/41/92/93.
- Communication: the receiver's reported total `comm_MB` (both directions, from the emp counters).

## 5. Results (single thread per party, fix build unless noted)

| ELL (bits+1) | t (δ) | n = n_S = n_R | reps | wall s (median) | **CPU s total (median)** [min–max] | receiver decrypt s | sender KDer s | comm MB | recv max RSS MB | check |
|---|---|---|---|---|---|---|---|---|---|---|
| 129 | 5 (4) | 16 | 3 | 93.1 | **93.7** [93.6–93.8] | 72.9 | 16.1 | 11.7 | 202 | 256/1280 ok |
| 129 | 5 (4) | 32 | 3 | 335.6 | **336.2** [330.4–339.7] | 295.7 | 32.5 | 22.0 | 223 | 1024/5120 ok |
| 129 | 5 (4) | 64 | 3 | 1231.0 | **1221.5** [1192.8–1242.2] | 1154.1 | 62.5 | 42.6 | 266 | 4096/20480 ok |
| 129 | 9 (8) | 16 | 3 | 168.9 | **167.7** [159.6–169.2] | 132.2 | 29.8 | 19.8 | 219 | 256/2304 ok |
| 129 | 9 (8) | 32 | 3 | 576.8 | **564.4** [559.6–566.3] | 501.6 | 64.9 | 38.1 | 258 | 1024/9216 ok |
| 32 | 8 | 16 / 32 / 64 | 1 each | 38.3 / 137.0 / 505.3 | 38.7 / 136.9 / 504.9 | 28.8 / 119.2 / 474.2 | – | 5.5 / 9.5 / 17.5 | ≤214 | ok |
| 16 | 8 | 128 | 1 | 994.4 | 985.8 | 964.9 | 24.5 | 17.3 | 214 | 16384/131072 ok |
| 129 (orig) | 5 | 16 / 32 | 1 each | 92.4 / 323.1 | 93.2 / 322.4 | 72.7 / 286.5 | – | 11.7 / 22.0 | – | (no counter; protocol result wrong, see patch 03) |

Not run:
- ELL=129, t=5 at k=7 and k=8: projected about 80 and 320 min, beyond the 30-min cap.
- ELL=129, t=9 at k ≥ 6: projected about 37 min, beyond the cap.

Raw data:
- `logs/<tag>/` holds `summary.json`, stdout of both parties, `time -v`, pre/post snapshots and `gate.txt`.
- `results.csv` holds every attempt, including excluded ones.
- `logs_excluded/` holds the throttled or killed attempts.

## 6. Fit and projection (`fit.py` → `fit_results.json`)

- Receiver decryption is purely quadratic: dec = a_d·ELL·t·n² with a_d = 4.44·10⁻⁴ s. The residual is within ±5 % over all 9 configurations. This is **57.3 ms per multi-pairing at ELL=129** (4·129+2 = 518 Miller-loop pairs plus one final exponentiation).
- Total CPU, global model: CPU = a·ELL·t·n² + b·ELL·t·n + c with a = 4.56·10⁻⁴, b = 1.21·10⁻⁴, c = 20.7 s. The residual is ≤ 6 % on 8 of 9 points (32 % on the smallest, 38 s point).
- Total CPU, per-series a·n² at ELL=129, t=5: a = 0.300 CPU-s per (x,y) pair. This is dominated by the n=64 point, residual +0.7 %. Smaller n are up to 18 % above the curve because of the fixed ~20 s OT/setup cost.
- In terms of the survey's earlier [EST] of about 0.5 CPU-s per pair (from the paper's 20-thread runs, assuming perfect speed-up): the measured single-core cost is about 0.30 CPU-s per pair, i.e. lower. At (ELL,t) = (32,8) the measured cost is 0.117 CPU-s per pair, against 0.20 implied by the paper's Table 2 times 20 threads.

**PROJECTION (not measured) — ELL = 129 (d = 128), t = 5 (δ = 4), balanced, single thread, CPU-s:**

| n | a·n² (series fit) | global model M2 | receiver decrypt only (lower bound) | vs ours 3.2 CPU-s @2^12 | vs otFPSI 368 CPU-s @2^12 |
|---|---|---|---|---|---|
| 2^10 | 3.15·10⁵ (3.6 CPU-days) | 3.08·10⁵ | 3.00·10⁵ | – | – |
| 2^12 | **5.04·10⁶ (58.3 CPU-days)** | 4.93·10⁶ (57.1 CPU-days) | 4.81·10⁶ | ≈ 1.5–1.6 ·10⁶ × | ≈ 1.3–1.4 ·10⁴ × |

- Communication at 2^12, from the linear fit of 11.7/22.0/42.6 MB, is ≈ 0.64 MB per element, i.e. ≈ 2.6 GB. This is a projection.
- Memory grows linearly with n: the receiver holds n·t·4·ELL G2 keys, about 1.3 GB at 2^12.
- A conservative paper wording: "≈ 5·10⁶ CPU-s (≈ 58 CPU-days) projected from same-host single-thread measurements up to n = 2^6, with the quadratic receiver term confirmed to ±5 %."

## 7. Caveats

1. Projection, not measurement, at 2^10 and 2^12. It is a 64× and 4096× extrapolation in n² from n = 64. The quadratic term is structural: the receiver loop is exactly n_S·n_R·t `decrypt` calls of constant cost, and the fit residual is ±5 %. Cache effects at larger n are not covered; the receiver working set at 2^12 is about 1.3 GB of keys, streamed sequentially.
2. Artifact bug (patch 03): the published code never returns a match. Our timings come from the one-token fixed build. The cost is unchanged, as shown by the orig-vs-fix runs in the table. This should be stated if the number is cited.
3. Transport: named pipes on one host replaced TCP because of the sandbox (patch 01). Neither our numbers nor theirs include LAN/WAN shaping. Communication is reported separately.
4. Inputs are the artifact's fixed vectors, not random ones. Cost is data-independent: every pair and every τ is tried. The receiver learns pairs with HD < t, which matches our δ = t−1.
5. Single thread per party. The authors' code is OpenMP-parallel over sender elements, so with p threads the wall time drops about p-fold but the CPU-s stay the same. The comparison is in CPU-s, consistent with "same thread count / algebraic cost".
6. Shared host. Other users' jobs ran unpinned throughout, and the cgroup was sometimes throttled. Runs with more than 100 throttle events per wall-minute were excluded and re-run (`logs_excluded/`). The kept k=6 runs saw 20–937 events over about 20 min, and their CPU-s spread is ±2 %.
7. The ELL convention: the paper's ℓ is our ELL (data bits = ELL−1). For d = 128 we used ELL = 129. Using ELL = 128 would be about 0.8 % cheaper.

## 8. Commands (reproduce)

```
# build: see §3; then
bash run_batch.sh batch1.txt     # orig vs fix at k=4
bash run_batch.sh batch2.txt     # main grid (skips completed tags)
/usr/bin/python3 fit.py          # table, fits, projections -> fit_results.json
# single run (example):
bash run_one.sh fuzzyPSICode/src_fix/build 6 6 129 5 fix_k6_ell129_t5_r1
#   = EMP_FIFO_DIR=fifo OMP_NUM_THREADS=1 taskset -c 40 party 6 6 129 5 1 &
#     EMP_FIFO_DIR=fifo OMP_NUM_THREADS=1 taskset -c 41 party 6 6 129 5 2
```
