# E2: paired headline, ours vs native otFPSI (d=128, m=n=2^12, δ ∈ {4,8})

Runs: 2026-11-12 06:36 → 07:34 PST, host <HOST> (2× Xeon Gold 6230R, 104 HW threads, user cgroup capped at 26 CPUs, shared; 1-min load 9–28 during the runs).

## Design

| Item | Value |
|---|---|
| Data | 5 independent synthetic data sets = `fpsi_bench -seed 1..5` (uniform, m/8 = 512 planted near-copies at 0..δ+2 flips; the generator in `impl/src/main.cpp`). Seeds 1..5 are the same seeds as `impl/scripts/run_seed_noU.sh` (the paper's "five independent data sets"). |
| Ours | `audit_r5/build/fpsi_bench` (sha256 0f2b985b…; libmipfpsi.a 67172db0… = impl/build11). Both parties in one process over coproto `LocalAsyncSocket`, one thread (`-nt 1`). Flags: `-m 4096 -n 4096 -d 128 -delta δ -b δ+1 -s 1 -nt 1 -indexverify -seed s`; rounded adds `-pow2`; U-free adds `-pow2 -noU` (as in campaigns 3–5). |
| Modes × reps | {exact, rounded, U-free} × δ {4,8} × 3 repetitions per seed = 90 runs. |
| otFPSI | Native otFPSI (xDup artifact, `01_otfpsi_cpu` sources = artifact + getrusage around the two protocol sections), `-proto_distance batched -proto_threshold np -querier_size 4096 -responder_size 4096 -vector_size 128 -threshold 4 -prf aes -ot softspoken` and the same with `-ot silent`, one run each per seed. Its cost is δ-independent (modulus 129 = d+1, m·n comparisons), so the per-seed run serves both δ rows. |
| Transport (deviation) | **The artifact only speaks TCP (`asioConnect("localhost:1212")`) and loopback TCP is blocked in this sandbox (a 2^6 test hung in connect). It has no in-process mode.** We therefore built `otfpsi_inproc`: the unmodified `01_otfpsi_cpu` sources, same compiler flags/libraries, with a shadow header `otfpsi_inproc/shim/coproto/Socket/AsioSocket.h` first on the include path that maps `AsioSocket` to coproto's in-memory `LocalAsyncSocket` and `asioConnect` to the two ends of a fresh pair. Protocol code, OT mode, two party threads and byte accounting are unchanged. Checks: comm at 2^8 = 13 921 392 B, at 2^12 = 3 334 MB (SoftSpoken) and 2 322 MB (silent), byte-identical to the TCP logs of campaign 3; both artifact self-checks ("Verification succeeded!" ×2) pass on every run; whole-process CPU 341–401 s (SoftSpoken) here vs 332–368 s user+sys in the TCP runs checked (campaign 5 `ot12_r1`, `ot12_r2`, campaign 3 `ot_soft_2e12_r1`; those include plaintext verification). The in-memory channel removes TCP syscall cost (TCP runs had 8–17 s sys), which favours otFPSI. |
| Order | One block per seed: {otFPSI SoftSpoken, otFPSI silent, 18 runs of ours} in random order (fixed RNG seed 20261112, see `jobs_e2.jsonl`), blocks in seed order. So each seed's otFPSI runs are within the same ~12-minute window as that seed's runs of ours. |
| Pinning | Every run under `taskset -c 40,41` (two physical cores; HT siblings 92/93 guarded: the runner waits if any benchmark binary pinned to 40/41/92/93 that it did not start exists, and if 1-min load ≥ 30). Strictly one run at a time. |
| Recorded | `/usr/bin/time -v` (user+sys, max RSS, wall), the tool's protocol-scoped CPU/wall/comm, `/proc/loadavg` and cgroup `nr_throttled` before/after, `ps -C …` before each run. |
| Throttle rule | Runs with > 100 throttled periods per minute are excluded and re-run (max 3 attempts). Two attempts were excluded (`e2_s3_d8_exact_r3` a1: 108/min, a2: 147/min); the third attempt (≤ 100/min) is used. All other runs: ≤ 22/min. |
| Correctness | All 90 final runs of ours (and the 2 excluded attempts) report `false_neg=0 false_pos=0` against the `-indexverify` plaintext index; exit code 0. |

CPU metric. "proto CPU" = both parties' CPU over the protocol only (ours: getrusage around the two coroutines; otFPSI: `PROTOCOL_CPU distance + threshold`, which excludes its random-input generation and plaintext verification). "proc CPU" = whole-process user+sys from `/usr/bin/time -v`. Ratios are reported for both; they differ by < 1.5%.

## otFPSI (native, in-process transport), one run per seed

| seed | mode | proto CPU s | proc CPU s | proto wall s | comm MiB | peak RSS MiB |
|---|---|---|---|---|---|---|
| 1 | SoftSpoken | 356 | 359 | 213 | 3 334 | 3 164 |
| 2 | SoftSpoken | 346 | 349 | 213 | 3 334 | 3 164 |
| 3 | SoftSpoken | 395 | 401 | 250 | 3 334 | 3 163 |
| 4 | SoftSpoken | 337 | 341 | 206 | 3 334 | 3 163 |
| 5 | SoftSpoken | 343 | 347 | 213 | 3 334 | 3 163 |
| 1 | silent | 399 | 403 | 263 | 2 322 | 6 431 |
| 2 | silent | 401 | 405 | 261 | 2 322 | 6 431 |
| 3 | silent | 399 | 403 | 257 | 2 322 | 6 432 |
| 4 | silent | 379 | 383 | 249 | 2 322 | 6 431 |
| 5 | silent | 392 | 395 | 258 | 2 322 | 6 432 |

SoftSpoken is the faster mode (median 346 vs 399 CPU-s, silent +15%), as in the paper. Silent sends 30% less.

## Ours: all 15 runs per (δ, mode)

| δ | mode | proto CPU s median [IQR] | proc CPU s median [IQR] | wall s median | comm MiB median [min–max] | peak RSS MiB median |
|---|---|---|---|---|---|---|
| 4 | exact | 4.19 [2.84–4.47] | 4.22 [2.88–4.50] | 4.19 | 12.6 [9.84–12.6] | 245 |
| 4 | rounded | 4.91 [2.82–5.33] | 4.96 [2.85–5.38] | 4.92 | 15.8 [9.84–15.8] | 330 |
| 4 | U-free | 6.14 [3.21–6.90] | 6.17 [3.24–6.95] | 6.15 | 185 [88.4–185] | 1 134 |
| 8 | exact | 13.8 [10.9–14.2] | 13.8 [11.0–14.3] | 13.8 | 42.9 [34.6–42.9] | 737 |
| 8 | rounded | 21.8 [11.2–25.3] | 21.8 [11.2–25.3] | 22.0 | 68.2 [34.6–68.2] | 1 159 |
| 8 | U-free | 26.5 [12.6–28.0] | 26.6 [12.7–28.1] | 26.6 | 849 [403–849] | 3 827 |

The spread is mostly data-driven (B): within a seed the max/min of the 3 repetitions is 1.01–1.16 for exact and U-free, 1.02–1.38 for rounded (host noise; same data and coins).

## Per-seed paired ratio otFPSI(SoftSpoken) / ours, protocol-scoped CPU

Ours = median of the seed's 3 repetitions; otFPSI = that seed's run.

| δ | mode | s1 | s2 | s3 | s4 | s5 | median [min–max] over seeds |
|---|---|---|---|---|---|---|---|
| 4 | exact | 123 | 78.5 | 68.1 | 80.5 | 125 | **80.5 [68.1–125]** |
| 4 | rounded | 123 | 64.2 | 61.2 | 66.5 | 125 | **66.5 [61.2–125]** |
| 4 | U-free | 109 | 55.6 | 56.7 | 52.7 | 115 | **56.7 [52.7–115]** |
| 8 | exact | 25.7 | 24.8 | 24.3 | 31.8 | 31.7 | **25.7 [24.3–31.8]** |
| 8 | rounded | 14.4 | 13.7 | 15.6 | 33.5 | 30.7 | **15.6 [13.7–33.5]** |
| 8 | U-free | 13.4 | 12.8 | 13.7 | 27.2 | 27.0 | **13.7 [12.8–27.2]** |

Against otFPSI silent (protocol CPU): δ=4 exact 91.2 [68.8–142], rounded 74.7 [61.8–143], U-free 64.6 [57.3–131]; δ=8 exact 28.8 [24.5–36.3], rounded 16.2 [15.8–37.6], U-free 15.0 [13.9–30.9]. Whole-process-CPU ratios are within 1.5% of these (`tables.md`).

## B/U/Q per seed

| δ | seed | exact | rounded | U-free (U not revealed) |
|---|---|---|---|---|
| 4 | 1 | 2/2/10 | 2/2/10 | 2/–/10 |
| 4 | 2 | 3/2/15 | 4/2/20 | 4/–/20 |
| 4 | 3 | 3/2/15 | 4/2/20 | 4/–/20 |
| 4 | 4 | 3/2/15 | 4/2/20 | 4/–/20 |
| 4 | 5 | 2/2/10 | 2/2/10 | 2/–/10 |
| 8 | 1 | 5/5/45 | 8/8/72 | 8/–/72 |
| 8 | 2 | 5/5/45 | 8/8/72 | 8/–/72 |
| 8 | 3 | 5/5/45 | 8/8/72 | 8/–/72 |
| 8 | 4 | 4/4/36 | 4/4/36 | 4/–/36 |
| 8 | 5 | 4/5/36 | 4/8/36 | 4/–/36 |

## Notes / anomalies

- The ratio is bimodal across seeds and tracks B: δ=4 seeds with B=2 (s1, s5) give ≈ 110–125×, seeds with B=3 (rounded to 4) give ≈ 53–80×; δ=8 seeds with B=5 (rounded to 8) give 13–16× in the rounded/U-free modes and 24–26× exact, seeds with B=4 give 27–34×.
- Compared with the paper text: paper's 5-data-set ranges were 50–102× (δ=4, rounded) and 12–26× (δ=8, rounded), 39–82× and 9–20× (U-free) against a single median otFPSI; here, paired per seed: 61–125× / 14–34× (rounded) and 53–115× / 13–27× (U-free). Partly higher because the otFPSI CPU here is protocol-scoped and uses the in-process channel, and because of the host state. The ordering exact > rounded > U-free holds for every seed.
- The δ=8 U-free mode sends 403–849 MiB (B=4 vs 8), up to 5× fewer bytes than SoftSpoken otFPSI (3 334 MiB) and 2.7× fewer than silent (2 322 MiB); peak RSS up to 3.8 GiB in one process (both parties).
- Another agent's `impl/build12/fpsi_bench` (e4_stress) ran pinned to core 44 during part of these runs; it did not use cores 40/41/92/93.

## Files and commands

| Item | Path |
|---|---|
| Job list (order as run) | `jobs_e2.jsonl` (generated by `../make_jobs.py`) |
| Runner | `../runner.py jobs_e2.jsonl runs` (driver `../run_all.sh`) |
| Raw | `runs/<tag>.a<attempt>.{log,time}`, `runs/summary.jsonl` (all attempts, incl. ps/load/throttle) |
| Machine-readable | `results.csv` (all attempts; `final=True` rows are the ones used) |
| Tables | `../aggregate.py e2` → `tables.md` |
| otFPSI in-process build | `../otfpsi_inproc/build.sh`, shim header `../otfpsi_inproc/shim/coproto/Socket/AsioSocket.h` |
| Env + sha256 | `../env.txt` |
