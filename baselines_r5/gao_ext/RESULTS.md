# Task A: Gao et al. (ASIACRYPT'24, Fmap Hamming FPSI) vs ours, same host and paired in time

Runs: 2026-11-10 05:22 → 06:43 PST (rep 1, part), then 2026-11-11 09:30 → 12:49 PST (rest of rep 1, reps 2–3). The first session was interrupted; the runner resumed from `runs/summary.jsonl` and skipped finished tags.

## Setup

| Item | Value |
|---|---|
| Host | 2× Xeon Gold 6230R @ 2.1 GHz (52 cores / 104 threads), shared. Load 9–30 during the runs. The user cgroup is capped at 26 CPUs. |
| Pinning | Every timed process ran under `taskset -c 42,43`; the runner sat on core 70. Cores 42/43 are distinct physical cores; their HT siblings 94/95 were not reserved. Before each run the runner logged `ps -C main,net_bench,fpsi_bench,otfpsi,simulation,run,flash-psi`. It would have waited for any foreign benchmark pinned to 42/43/94/95; none was. |
| Concurrency | Exactly one timing run at a time (`runner.py`). Other users' unpinned jobs were running on the host (e.g. a separate Blass–Noubir agent, audit_r5 net_bench on cores 44/45). |
| Recorded per run | `/usr/bin/time -v` (user+sys CPU, max RSS), `/proc/loadavg` and cgroup `nr_throttled` before and after, in `runs/summary.jsonl`. |
| Gao binary | Untouched artifact `code_repro/Fuzzy-Private-Set-Intersection-from-Fuzzy-Mapping/build/main` (git 217e4f3), run as `main -fpsi -t13 -hams L -hamr L -hamdelta δ -hamd 128 -hami 6`. |
| Gao online CPU | Process CPU sampled from `/proc/pid/stat` when the artifact prints "offline done". Online CPU = total CPU − that mark. |
| Gao comm | Printed by the artifact, in MiB (2^20 bytes). |
| Our binary | `build_ours/fpsi_bench`. It is built from the same `impl/` sources as `impl/build11`: `libmipfpsi.a` and `net_bench` are byte-identical (sha256 e4d16199…). |
| Why `fpsi_bench`, not `net_bench` | **Loopback TCP is blocked in this agent sandbox**: every connect to 127.0.0.1 or ::1 times out, even with the sandbox disabled (`tcp_probe.py`). So the two-process `net_bench` could not run. `fpsi_bench` runs both parties in one process over an in-memory socket, single thread (`-nt 1`). Gao's artifact does the same (two threads over `LocalAsyncSocket`), so the comparison is like-for-like. |
| Our CPU | Both parties together. |
| Our flags | `-m n -n n -d 128 -delta δ -b δ+1 -s 1 -nt 1 -indexverify -seed <crc32(tag)>`. The rounded mode adds `-pow2`. |
| Our correctness | Every run was checked against the exact plaintext index: all 60 runs had FN = FP = 0. Every Gao run returned the 6 planted matches. |

Our protocol has no offline phase, so its "online" time is its total time.

## Results, d = 128

Values are the median and [min–max] over 3 runs (1 run for the control). CPU is user+sys of the whole process.

| Protocol | δ | m=n | Online wall s | Online CPU s | Total wall s | Total CPU s | Comm MiB | Max RSS MB |
|---|---|---|---|---|---|---|---|---|
| Gao | 4 | 2^8 | 4.39 [3.71–4.50] | 4.40 [4.01–4.84] | 159.8 [156.2–165.2] | 159.8 [156.7–165.8] | 91.9 | 385 |
| Gao | 4 | 2^10 | 18.46 [14.88–18.91] | 18.71 [16.44–20.34] | 672 [670–822] | 674 [668–700] | 367.5 | 1 513 |
| Gao | 4 | 2^12 | 73.3 [59.5–73.9] | 73.1 [66.3–81.3] | 2 713 [2 578–3 207] | 2 673 [2 575–2 864] | 1 470.1 | 6 059 |
| Ours exact | 4 | 2^8 | — | — | 0.40 [0.33–0.57] | 0.40 [0.33–0.41] | 1.33 [1.16–1.33] | 50 |
| Ours exact | 4 | 2^10 | — | — | 1.00 [0.82–1.22] | 1.01 [0.83–1.22] | 2.73 | 69 |
| Ours exact | 4 | 2^12 | — | — | 4.32 [3.47–4.85] | 4.36 [3.52–4.87] | 12.6 [8.7–12.6] | 245 |
| Ours pow2 | 4 | 2^8 | — | — | 0.44 [0.40–0.82] | 0.41 [0.40–0.44] | 1.33 | 50 |
| Ours pow2 | 4 | 2^10 | — | — | 1.19 [0.98–1.36] | 1.19 [0.99–1.37] | 2.73 [2.73–4.42] | 68 |
| Ours pow2 | 4 | 2^12 | — | — | 3.43 [2.63–9.50] | 3.48 [2.67–9.45] | 9.84 [8.74–15.80] | 165 |
| Gao (unpatched, P=8) | 8 | 2^7 | 6.76 [6.25–6.78] | 7.73 [7.12–7.74] | 200.5 [181.6–204.1] | 198.4 [182.3–199.2] | 116.4 | 446 |
| Gao-P (bit-level P=14) | 8 | 2^8 | 8.43 [8.15–10.31] | 9.25 [8.12–10.16] | 313.5 [290.9–356.5] | 312.6 [291.0–331.6] | 165.6 | 687 |
| Gao-P (bit-level P=14) | 8 | 2^10 | 29.7 [27.6–30.0] | 33.4 [31.3–33.8] | 1 216 [1 142–1 234] | 1 205 [1 142–1 220] | 662.3 | 2 729 |
| Gao-P | 8 | 2^12 | **not run**: projected total = 4 × 1 142 s ≈ 4 570 s > 1 h | | | | (≈ 2 650 projected) | |
| Ours exact | 8 | 2^7 / 2^8 / 2^10 / 2^12 | — | — | 0.44 / 0.79 / 2.12 / 15.9 [10.4–15.9] | 0.44 / 0.77 / 2.13 / 15.9 | 1.38 / 2.37 / 7.19 / 40.8 | 53 / 55 / 101 / 708 |
| Ours pow2 | 8 | 2^7 / 2^8 / 2^10 / 2^12 | — | — | 0.44 / 0.63 / 2.84 / 23.1 [10.3–25.8] | 0.44 / 0.63 / 2.85 / 23.2 | 1.38 / 1.95 / 9.25 / 59.6 [34.6–68.2] | 53 / 54 / 154 / 1 120 |
| Control: Gao-P binary without `-hambits` | 4 | 2^8 | 4.29 | 4.48 | 168.5 | 168.7 | 91.9 | 386 |

Our run-to-run spread is wider than Gao's. Two causes:
- The data seed differs per run, so the revealed load B changes (e.g. B = 2 or 3 at 2^12, δ = 4). B in turn changes communication (8.7 vs 12.6 MiB).
- Host load. For example, `ours_pow2_d4_2e12_r3` = 9.5 s ran at load ≈ 27.

The **CPU columns are the robust metric**. Several long Gao runs were cgroup-throttled: `nr_throttled` rose by up to 9 845 periods during `gao_d4_2e12_r1`, whose wall time 3 207 s exceeds its CPU time 2 864 s.

## Paired ratios, Gao / ours

Each ratio pairs runs from the same repetition, which ran back-to-back. Values are median [min–max] over the 3 pairs.

| Setting | Ours mode | Online-CPU ratio (Gao online / ours total) | Total-CPU ratio | Comm ratio |
|---|---|---|---|---|
| δ=4, 2^8 | exact | 10.8 [10.0–14.6] | 393 [392–500] | 68.9 [68.9–79.0] |
| δ=4, 2^8 | pow2 | 10.8 [10.1–11.1] | 391 [379–395] | 68.9 |
| δ=4, 2^10 | exact | 19.9 [15.6–20.3] | 673 [556–849] | 134.5 |
| δ=4, 2^10 | pow2 | 15.8 [12.1–20.9] | 562 [515–691] | 134.5 [83.1–134.5] |
| δ=4, 2^12 | exact | 15.3 [15.1–23.4] | 596 [551–824] | 116.7 [116.7–168.2] |
| δ=4, 2^12 | pow2 | 23.7 [7.8–25.2] | 834 [284–978] | 149.4 [93.0–168.2] |
| δ=8, 2^7 (unpatched Gao) | exact / pow2 | 17.1 / 17.1 | 438 / 438 | 84.3 / 84.3 |
| δ=8, 2^8 (Gao-P) | exact | 12.8 [12.1–14.2] | 419 [408–508] | 69.9 [69.9–84.9] |
| δ=8, 2^8 (Gao-P) | pow2 | 14.5 [14.0–16.1] | 503 [490–524] | 84.9 |
| δ=8, 2^10 (Gao-P) | exact | 15.8 [15.5–15.9] | 568 [567–575] | 92.1 |
| δ=8, 2^10 (Gao-P) | pow2 | 11.9 [10.8–11.9] | 424 [395–432] | 71.6 |
| δ=8, 2^12 [EST] | exact | Gao-P not run. Projected total ≈ 4 570 CPU-s vs our 15.9 s gives ≈ 290× | | ≈ 65× |

How to read these:
- Against Gao's paper metric (online only), we are **≈ 11–24× less CPU**.
- Counting Gao's input-independent but non-reusable GM precomputation, we are **≈ 400–830× less CPU**.
- We use **≈ 70–170× less communication**.

Gao's online CPU/wall here (73 s at 2^12) matches the paper's 76 s and our earlier 62–96 s. Its total (2 575–2 864 CPU-s) matches the earlier 2 635–3 756 s.

At δ=8 our cost grows faster than Gao's (15.9 s at 2^12 vs 4.3 s at δ=4), so the ratio narrows with δ.

## δ = 8 support in the Gao artifact

**The unmodified artifact cannot run d=128, δ=8 at n ≥ 2^8.** The chain is:
1. `frontend/fpsi.cpp` sets the super-component width to P = ⌊⌊d/(δ+1)⌋/8⌋·8. At d=128, δ=8 that is ⌊14/8⌋·8 = **8 bits**. The rounding down to bytes is needed because the synthetic-data generator writes the UniqC index bytewise.
2. A guard then requires 2^P > n (`pow(2, side_length) <= recv_set_size` → abort).
3. With P = 8 that allows only n ≤ 255. Running `-hamdelta 8 -hams 8 -hamr 8` prints `pow(2, side_length) should be greater than recv_set_size` and exits.
4. `-hamside` cannot help, because it must be ≤ the same rounded value (8).
5. At n = 2^10 the problem is more than a guard. With 8-bit components, the 16 positions offer at most 16·256 = 4 096 unique (position, value) slots, but UniqC needs (δ+1)·n = 9 216 of them. So R.UniqC is unsatisfiable by pigeonhole.
6. With P = 8 the artifact can run only n ≤ 2^7 at δ=8. We ran that point unmodified (`gao_d8_2e7`).

**What we did for 2^8 and 2^10.** The Fmap/GM protocol code in `FPSI-for-Hamming/Hamming.cpp` (`receiver_key_hamming` and `sender_key_hamming`) reads super-components bit by bit, so it supports any P. Only the guard and the data generator assume byte alignment. In a copy (`patched/src`, original untouched) we added a flag `-hambits`. It:
- sets P = ⌊d/(δ+1)⌋ = 14;
- relaxes the byte guards (keeping 2^P > n and P(δ+1) ≤ d);
- writes the UniqC index bit by bit.

The protocol code is unchanged, and this is how the paper's scheme is parameterised for general δ. Two checks:
- Control: the patched binary without the flag matches the original at δ=4, 2^8 (168.7 vs 159.8–165.8 CPU-s; same 91.9 MiB).
- Every patched run found the 6 planted matches.

**Report Gao-P as "Gao's protocol, artifact with bit-level packing patch", not as the unmodified artifact.**

## Files and commands

| Item | Path |
|---|---|
| Runner | `runner.py` (gating, ps/loadavg/nr_throttled logging, `/usr/bin/time -v`, offline-mark CPU sampling) |
| Job list | `make_jobs.py` → `jobs_A.jsonl` (interleaved per rep: Gao, ours exact, ours pow2 for each size) |
| Resume | `../run_all_AB.sh` (A then B) |
| Tables | `aggregate.py A runs/summary.jsonl` |
| Raw data | `runs/*.log`, `runs/*.time`, `runs/summary.jsonl` |
| Gao-P | `patched/src` (diff: `frontend/fpsi.cpp` only), `patched/build/main` |
| Loopback check | `tcp_probe.py` |

## Caveats

- Both Gao and our runs use an in-memory channel, so there is no network cost. A two-process TCP run of ours (`net_bench`) was impossible in this sandbox. Earlier campaigns (`results/campaign5_*`) cover that.
- The host was shared and loaded (load 9–30), and throttling occurred during long Gao runs. Use CPU seconds and paired ratios.
- Gao's data generator forces R.UniqC. Our data is uniform with planted near pairs. Both are synthetic.
- Our exact 2^12, δ=4 median here (4.3 CPU-s) is higher than the ≈ 2.7 s quoted in the survey. The cause is host load plus seeds with B = 3. The paired ratio is what is comparable.
