# E4 — receiver-load stress and m/n sweep (in-process, build12)

Date 2026-11-12 (PDT). Binary `impl/build12/fpsi_bench` (library bit-identical to audit_r5, see
`../e5_abort/sha256_compare.txt`). d = 128, δ = 4, block cover (5,1) (T = 5, ℓ_t = 25/26, r = 0),
nt = 1, ground truth `-indexverify`. Modes as in the paper's campaigns: exact (no flag), rounded
(`-pow2`), U-free (`-pow2 -noU`). Allowed fallback (default). Every run: `run1.sh` = taskset -c 44,45,
/usr/bin/time -v, one at a time, loadavg and user-slice cgroup nr_throttled before/after, 100 GiB
address-space cap (prlimit). CPU = getrusage of the protocol phase (both parties, one process), comm =
bytes sent both ways, RSS = /usr/bin/time max RSS (both parties + ground truth).
Files: `runs.csv` (one row per run, 164 runs), `summary.csv` (medians), `logs/`, drivers `run_e4.sh`,
`rerun_noise.sh`, data generator `gen_clusters.py`, parser `parse_e4.py`.

Host: shared, loadavg 13–28 during the runs; another user's fpsi_bench (audit_r5/build, all-pairs) ran
concurrently on cores 40,41 from ~07:20 on (not started by me, not touched). User-slice nr_throttled
deltas were 0–120 per run (cgroup-wide counter, not specific to these runs).

**Correctness: all 163 completed runs report false_neg=0 false_pos=0** (exit 0). The one failed run is
U-free at B = 512, which hit the 100 GiB cap (std::bad_alloc in R at RSS 56 GiB; both parties then
aborted cleanly through the E5 harness fix, exit 3). No wrong output in any run.

## (a) Receiver clusters, n = m = 2^12
k receiver vectors share one value on the bits of table 0, the rest is uniform; rejection sampling keeps
every other bucket at load 1, so B = k exactly (k = 1: collision-free receiver set). Sender: uniform with
512 near-copies (0–6 flips) of distinct receiver vectors, min(k, 8) of them cluster members. |I| = 348–373.
Medians of 3 runs for B ≤ 64, 1 run above. nTU = programmed OPPRF keys (n·T·U; n·T in U-free).

| B | mode | U | Q | nTU | branch | CPU s (min–max) | comm MiB | RSS GiB |
|---|---|---|---|---|---|---|---|---|
| 1 | exact | 2 | 5 | 40,960 | index | 1.48 (1.46–1.49) | 6.6 | 0.08 |
| 1 | rounded | 2 | 5 | 40,960 | index | 1.47 | 6.6 | 0.08 |
| 1 | U-free | – | 5 | 20,480 | index | 1.54 | 42.6 | 0.24 |
| 2 | exact | 2 | 10 | 40,960 | index | 2.79 (2.69–2.90) | 9.8 | 0.16 |
| 2 | rounded | 2 | 10 | 40,960 | index | 2.85 | 9.8 | 0.16 |
| 2 | U-free | – | 10 | 20,480 | index | 3.13 | 88.4 | 0.53 |
| 4 | exact | 2 | 20 | 40,960 | index | 4.97 (4.97–5.02) | 15.8 | 0.32 |
| 4 | rounded | 2 | 20 | 40,960 | index | 5.03 | 15.8 | 0.32 |
| 4 | U-free | – | 20 | 20,480 | index | 6.04 | 184.9 | 1.11 |
| 8 | exact | 5 | 40 | 102,400 | index | 10.61 (10.61–10.66) | 31.1 | 0.61 |
| 8 | rounded | 8 | 40 | 163,840 | index | 10.83 | 34.2 | 0.61 |
| 8 | U-free | – | 40 | 20,480 | index | 12.73 | 390.0 | 2.09 |
| 16 | exact | 4 | 80 | 81,920 | index | 21.88 (20.55–21.92) | 55.1 | 1.23 |
| 16 | rounded | 4 | 80 | 81,920 | index | 20.92 | 55.1 | 1.23 |
| 16 | U-free | – | 80 | 20,480 | index | 28.54 | 822.4 | 3.83 |
| 32 | exact | 3 | 160 | 61,440 | index | 44.94 (41.44–45.57) | 104.6 | 2.60 |
| 32 | rounded | 4 | 160 | 81,920 | index | 47.00 | 105.7 | 2.44 |
| 32 | U-free | – | 160 | 20,480 | index | 58.19 | 1,733 | 7.91 |
| 64 | exact | 4 | 320 | 81,920 | index | 106.4 (99.2–116.1) | 207.4 | 5.18 |
| 64 | rounded | 4 | 320 | 81,920 | index | 98.9 | 207.4 | 5.18 |
| 64 | U-free | – | 320 | 20,480 | index | 128.6 | 3,646 | 16.5 |
| 128 | exact | 3 | 640 | 61,440 | index | 170.2 | 411.7 | 10.8 |
| 128 | rounded | 4 | 640 | 81,920 | index | 165.6 | 412.7 | 10.8 |
| 128 | U-free | – | 640 | 20,480 | index | 248.8 | 7,659 | 34.5 |
| 512 | exact | 5 | 2,560 | 102,400 | index | 798.8 | 1,663 | 45.7 |
| 512 | rounded | 8 | 2,560 | 163,840 | index | 884.6 | 1,666 | 45.7 |
| 512 | U-free | – | 2,560 | – | index | **cap: bad_alloc at 56 GiB RSS (> 100 GiB AS)** | – | 55.9 |
| 2048 | exact | – | 10,240 | – | **all-pairs** | 1,030.4 | 1,788 | 12.2 |
| 2048 | rounded | – | 10,240 | – | **all-pairs** | 963.5 | 1,788 | 12.2 |
| 2048 | U-free | – | 10,240 | – | **all-pairs** | 985.0 | 1,788 | 12.2 |

Observations
- Cost grows linearly in B as predicted by Q = B·ΣV: CPU per unit of B ≈ 1.3–1.7 s for exact
  (1.48 s at B = 1, 106 s at B = 64, 799 s at B = 512). Rounded = exact here because B is a power of
  two (only U is rounded: 5 → 8 raises nTU).
- U-free costs 1.04× (B = 1) to 1.46× (B = 128) the CPU of exact but its traffic is 6.5–19× higher
  (42.6 → 7,659 MiB) and its memory 2.9–3.4× higher; at B = 512 it does not fit in 100 GiB.
- Fallback: the index branch is kept through B = 512 (cost model: N·(dc+τ+log N) ≈ 1.8·10^9 <
  m·n·d ≈ 2.1·10^9); at B = 2048 all three modes switch to all-pairs (U is then never computed or sent)
  and take 963–1,030 CPU-s, 12.2 GiB. Measured: index at B = 512 (799 s) is still below all-pairs
  (≈ 1,000 s), so the decision is consistent with measurement at these two points, but the index branch
  at B = 512 needs 3.7× the memory of all-pairs (45.7 vs 12.2 GiB).

## (b) m/n sweep (fpsi_bench synthetic generator, seed 1, plant = min(m,n)/8)
Pass 1 (r1–r3) ran at loadavg ≈ 21–28 and showed up to 2× spread at 2^12 (e.g. 3.27/5.34/5.60 s);
pass 2 (r4–r6, `rerun_noise.sh`, loadavg ≈ 15–24) is tight and is reported here; pass 1 stays in
`summary.csv` (pass = 1). Medians of 3 runs (1 run at 2^16). All runs exact (0 FN / 0 FP), all index branch.

| m | n | mode | B | U | Q | nTU | CPU s (min–max) | comm MiB | RSS GiB | |I| |
|---|---|---|---|---|---|---|---|---|---|---|
| 2^8 | 2^12 | exact | 2 | 1 | 10 | 20,480 | 0.46 | 2.4 | 0.05 | 24 |
| 2^8 | 2^12 | U-free | 2 | – | 10 | 20,480 | 0.46 | 6.0 | 0.05 | 24 |
| 2^10 | 2^12 | exact | 3 | 1 | 15 | 20,480 | 1.26 | 4.6 | 0.08 | 78 |
| 2^10 | 2^12 | U-free | 4 | – | 20 | 20,480 | 1.66 | 42.4 | 0.24 | 78 |
| 2^12 | 2^12 | exact | 2 | 2 | 10 | 40,950 | 2.88 (2.86–2.90) | 9.8 | 0.16 | 352 |
| 2^12 | 2^12 | U-free | 2 | – | 10 | 20,475 | 3.23 (3.19–3.26) | 88.4 | 0.53 | 352 |
| 2^14 | 2^12 | exact | 2 | 2 | 10 | 40,960 | 11.11 (10.94–11.15) | 29.1 | 0.61 | 377 |
| 2^14 | 2^12 | U-free | 2 | – | 10 | 20,480 | 13.39 | 390.9 | 2.10 | 377 |
| 2^16 | 2^12 | exact | 1 | 2 | 5 | 40,960 | 22.72 | 57.8 | 1.23 | 362 |
| 2^16 | 2^12 | U-free | 1 | – | 5 | 20,480 | 26.79 | 827.1 | 3.84 | 362 |
| 2^12 | 2^8 | exact | 1 | 2 | 5 | 2,560 | 1.44 | 4.6 | 0.07 | 23 |
| 2^12 | 2^8 | U-free | 1 | – | 5 | 1,280 | 1.60 | 41.6 | 0.24 | 23 |
| 2^12 | 2^10 | exact | 2 | 2 | 10 | 10,240 | 2.77 | 8.2 | 0.16 | 82 |
| 2^12 | 2^10 | U-free | 2 | – | 10 | 5,120 | 3.12 | 87.5 | 0.53 | 82 |
| 2^12 | 2^14 | exact | 3 | 2 | 15 | 163,830 | 5.00 | 18.7 | 0.24 | 376 |
| 2^12 | 2^14 | U-free | 4 | – | 20 | 81,915 | 6.86 | 188.0 | 1.12 | 376 |
| 2^12 | 2^16 | exact | 3 | 2 | 15 | 655,360 | 7.16 | 43.6 | 0.30 | 364 |
| 2^12 | 2^16 | U-free | 4 | – | 20 | 327,680 | 7.71 | 200.4 | 1.13 | 364 |

(U-free reveals the rounded B, hence 3 → 4.) |I| = output size = truth.

Observations
- At fixed n the cost grows with m·Q, sublinearly in m because of fixed per-run costs and B varying
  1–3 (0.46 → 22.7 s for 256× more m; 2.88 → 22.7 s, 7.9×, from 2^12 to 2^16 with B 2 → 1);
  at fixed m it grows only through B and the nT(U) programmed keys (2.88 s at n = 2^12 → 7.16 s at
  n = 2^16, 16× more n, B = 2 → 3).
- U-free traffic is 2.5–14× that of exact; CPU 1.0–1.4×.
