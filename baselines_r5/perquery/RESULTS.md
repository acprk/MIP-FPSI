# Task B: per-query (unbalanced, probabilistic) baselines vs ours with m = N, n = 1

Runs: 2026-11-11 12:49–13:38 PST. Each job ran alone, pinned with `taskset -c 42,43`, using the same runner and logging as `../gao_ext` (`/usr/bin/time -v`, loadavg, `nr_throttled`, ps listing). Host: 2× Xeon Gold 6230R @ 2.1 GHz, shared, load 15–30.

## What was built

| Protocol | Status |
|---|---|
| **Bui–Cong CANS'25 = flash-psi** (github.com/kc1212/flash-psi @ cd7e707) | **Built and run.** Swanky (pinned fork kc1212/swanky@2ebb4f4) was fetched as a 10 MB codeload tarball, and `Cargo.toml` was pointed at it by path (same commit). Rust 1.94.0; it needs ≥ 1.85 because of newer `clap`/`rmp-serde`. Built single-thread with `cargo build --release --bin simulation -j 4`; the build took about 2.5 min. |
| **FLPSI (Uzun et al., USENIX'21)** | No public code (survey §6). The "flash-psi" repo **is** the Bui–Cong implementation, with "flpsi" as its simulation mode name. **Not run: no artifact exists.** |

**Patch to flash-psi** (`flash-psi/src/bin/simulation.rs`, `--full-scan` flag):
- The shipped simulation places the true match at index m/2 and lets the receiver stop decoding there (`receive(upto = m/2, …)`). That skips half the receiver work, which a real query cannot do because it does not know where the match is.
- `--full-scan` sets `upto = m−1` and checks that the planted match is *contained* in the output. FPR is reported over all m entries.
- Both variants were run. The full-scan one is the fair per-query number.

The artifact also has a `--padding` variant (F128b/F64b label padding). It was run as `padfull`.

## Commands

| Run | Command |
|---|---|
| flash-psi | `simulation -m N --weight 14 --subsample-count 64 -t 2 --track-io [--full-scan] [--padding]` |
| Ours | `fpsi_bench -m N -n 1 -d 128 -delta 4 -b 5 -s 1 -nt 1 -plant 1 -seed 2 -indexverify [-nofallback]` |

The flash-psi parameters are the paper's: d = 128, weight d_s = 14, T = 64, t = 2. Its inputs are random 128-bit vectors, and the query equals one DB entry. It runs one process with two threads over a UnixStream pair. Its reported time is "total duration" = online phase only. The dummy VOLE / OT setup, the garbling and the TLPSI setup are excluded, matching the paper's preprocessing split.

Ours: sender = database (m = N), receiver = 1 query, exact Hamming δ = 4, both parties in one process (in-memory socket). As in `../gao_ext`, loopback TCP is blocked in this sandbox, so `net_bench` could not be used. `fpsi_bench` is the same library (`libmipfpsi.a` identical to `impl/build11`).

**net_bench does support m ≠ n**: it takes `-m` and `-n` separately and generates both sets independently. n = 1 works. By default our cost model picks its all-pairs fallback for n = 1 (`fallback=1`). `-nofallback` forces the main bucketed path.

## Results

Values are the median [min–max] of 3 runs.
- flash-psi "wall" is its online duration. Its "process CPU" includes the excluded setup.
- Ours has no offline phase; its CPU covers both parties for the whole protocol.

| Protocol | N | Online / protocol wall s | Process CPU s | Comm MiB | Max RSS MB | Correctness |
|---|---|---|---|---|---|---|
| flash-psi shipped (decodes up to m/2) | 2^12 | 2.24 [2.22–2.26] | 8.50 | 6.41 | 81 | FP rate 0 |
| flash-psi full scan | 2^12 | **4.35 [4.27–4.36]** | 10.61 | **6.41** | 81 | FP/entry ∈ {0, 2.4e-4}, i.e. 0–1 false match per query |
| flash-psi full scan + padding | 2^12 | 9.96 [9.57–10.21] | 32.33 | 12.44 | 110 | FP 0 |
| flash-psi shipped | 10^4 | 5.33 [5.25–5.35] | 20.06 | 15.28 | 133 | — |
| flash-psi full scan | 10^4 | 10.50 [10.48–11.04] | 25.39 | 15.28 | 133 | — |
| flash-psi shipped | 2^16 | 34.8 [34.3–34.9] | 129.7 | 98.75 | 703 | FP/entry up to 6.1e-5 |
| flash-psi full scan | 2^16 | **68.4 [66.9–68.7]** | 162.5 | **98.75** | 703 | 0, 1, 3 false matches per query |
| flash-psi full scan + padding | 2^16 | 153.7 [151.8–154.8] | 503.7 | 195.25 | 1 253 | 0–1 false matches |
| **Ours** (default; picks all-pairs path) | 2^12 | **0.306 [0.305–0.320]** | 0.32 | **0.97** | 42 | FN = FP = 0 (planted match found) |
| Ours `-nofallback` | 2^12 | 1.37 [1.37–1.39] | 1.39 | 4.43 | 75 | FN = FP = 0 |
| **Ours** (default; picks all-pairs path) | 2^16 | **3.28 [3.27–3.42]** | 3.38 | **12.14** | 104 | FN = FP = 0 (the planted pair fell outside δ, so truth = 0) |
| Ours `-nofallback` | 2^16 | 21.8 [19.8–22.2] | 21.9 | 55.64 | 1 260 | FN = FP = 0 |

The paper's reported point is 1.69 s / 15.5 MB at m = 10^4 on an i9-9900 @ 3.1 GHz (survey §7). Our shipped-mode re-run gives 5.33 s / 15.3 MiB on this 2.1 GHz, loaded Xeon: communication matches, time is 3.2× slower. That fits the clock difference plus load, and the paper possibly using the m/2 shortcut.

## Per-query ratios: flash-psi full scan / ours

Same host, one run of each per repetition, run back-to-back.

| N | Ours default: time | Ours default: comm | Ours `-nofallback`: time | Ours `-nofallback`: comm |
|---|---|---|---|---|
| 2^12 | **14×** (4.35 / 0.306 s) | **6.6×** (6.41 / 0.97 MiB) | 3.2× | 1.4× |
| 2^16 | **21×** (68.4 / 3.28 s) | **8.1×** (98.75 / 12.14 MiB) | 3.1× | 1.8× |

Notes on these ratios:
- They are conservative for us. flash-psi's time excludes its preprocessing (VOLE, garbling), while ours is end-to-end.
- flash-psi's online phase runs two threads concurrently, so its online CPU can be up to 2× its wall.
- Against the shipped m/2 mode the ratios halve (7× / 11×).
- Ours is exact; flash-psi is probabilistic.

## False negatives / false positives (probabilistic baseline)

The artifact uses independent uniformly random weight-14 masks over d = 128 (`src/subsample.rs`). It declares a match if ≥ t = 2 of the T = 64 subsamples agree. Exact per-pair rates under this design (`subsample_rates.py`):

| HD(query, entry) | 4 | 8 | 10 | 16 | 20 | 25 | 30 |
|---|---|---|---|---|---|---|---|
| FNR | ≈ 0 | 1.3e-12 | 3.5e-9 | 8.3e-4 | **3.1 %** | 27.5 % | 66 % |

- **FPR for a uniformly random 128-bit entry is 2.95e-5.** That is ≈ 0.12 false matches per query at N = 2^12 and ≈ 1.9 at N = 2^16.
- Observed: full scan at 2^16 gave 0, 1 and 3 false matches over 3 runs, i.e. 2.0e-5 per entry, consistent with the model.
- **The artifact's simulation never tests false negatives.** The query is an exact copy of a DB entry (HD = 0), so FN cannot occur in its runs.
- The paper (§6.2, survey §7) claims FPR < 7e-6 and FNR < 2e-16 "for d_k ≤ 20". With the d = 128, d_s = 14, T = 64, t = 2 masks in the code, those figures do **not** hold. FPR is ≈ 4× higher, and FNR at HD = 20 is ≈ 3 %. The claimed numbers match d = 256, where FPR ≈ C(64,2)·2^-28 ≈ 7.5e-6.
- **This is worth stating carefully if Bui–Cong is cited.** At our δ = 4 their FNR is negligible, but they also do not stop at δ: entries at HD 5–15 are matched with high probability. The functionality is "approximately HD ≲ 10–16", not "HD ≤ 4".

## Caveats

- flash-psi uses dummy (trusted-dealer) setup for VOLE/OT and pre-garbled circuits; the paper treats this as preprocessing. Its process CPU (e.g. 162.5 s at 2^16) includes that setup, so read only its online wall time.
- flash-psi is **labeled** PSI; ours is unlabeled. That costs flash-psi extra label work.
- Both runs are single-process with no network shaping.
- In our 2^16 runs the planted pair (random 0..δ+2 flips) landed above δ, so those runs have no true match. Correctness was still verified (output = truth = ∅). The 2^12 runs had one true match, which was found.

## Files

| Item | Path |
|---|---|
| Patched artifact | `flash-psi/` (`Cargo.toml` path deps; `src/bin/simulation.rs` `--full-scan`) |
| Swanky fork source | `swanky-2ebb4f432db6b1e0f796b1842e1c76157d21145f/` |
| Job list | `make_jobs_B.py` → `jobs_B.jsonl` |
| Raw data | `runs/` |
| Tables | `../gao_ext/aggregate.py B runs/summary.jsonl runs` |
| Rate model | `subsample_rates.py` |
