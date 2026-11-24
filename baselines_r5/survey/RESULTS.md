# Task C: van Baarsen–Pu (EUROCRYPT'24) and Hua et al. (USENIX'22). Can they run exact Hamming FPSI?

Checked on 2026-11-10 (PST). The source tarballs were downloaded with `curl` from GitHub codeload and are kept here:
- `fuzzy_PSI-HEAD/` (11 KB)
- `sbb-HEAD/` (9.4 MB)

Nothing was run. Neither artifact can do a balanced exact-Hamming run at d=128, δ=4, n=2^8. The reasons are below.

## 1. van Baarsen & Pu, "Fuzzy PSI with Large Hyperballs" (EUROCRYPT 2024, ePrint 2024/330)

| Item | Finding |
|---|---|
| Code | https://github.com/sihangpu/fuzzy_PSI. Rust, under 1 000 lines; the authors call it a proof of concept. No CLI; it runs only through `cargo bench`. |
| Metrics implemented | L∞, L1 and L2 over integer points (`src/psi.rs`: `l1_dist`, `l2_dist`, `metric` ∈ {1, 2}, L∞ by default). **There is no Hamming code.** |
| Dimension | Fixed at compile time: `pub const DIM: usize = 2`. `intersection()` panics with "DIM should be 2" for the L1/L2 paths. Every sender point enumerates `BLK_CELLS = 1 << DIM` cells, so the cost is exponential in DIM. |
| Hamming as L1 over {0,1}^128 | Would need DIM = 128. That means 2^128 cells per point plus a rewrite of the L1/L2 intersection code. **Infeasible.** |
| Verdict | **Not runnable for d=128 Hamming in any time budget.** It is a low-dimensional Lp protocol. |

The follow-up paper, van Baarsen–Pu, "Fuzzy PSI from VOLE" (ASIACRYPT 2025, ePrint 2025/911), handles Minkowski distances only. We found no public code for it.

## 2. Hua, Namavari, Cheng, Naaman, Ristenpart (USENIX Security 2022): SBB

| Item | Finding |
|---|---|
| Code | https://github.com/vegetable68/sbb. Python (Flask server on a GPU box), plus Go (CIRCL OPRF, protobuf), plus an EMP-sh2pc C++ program (`emp/example.cpp`, built from `emp/Dockerfile`), plus CrypTen. 5 commits, last push 2021. |
| What the EMP part computes | `test_approx_match`: one client 256-bit PDQ hash against a bucket of `bucket_size` server hashes. Each bit is a 16-bit `Integer`; the program sums the XORs, then outputs OR_j [HD_j < threshold] as **one masked bit**. Bit width is hard-coded (`size = 256`, a 64-hex-character input line). |
| Functionality | One query against one bucket, giving a single yes/no bit. It does not output the matching sender items. SBB itself deliberately leaks a noisy bucket (LSH plus bit-flip noise) to the server, so it is **not a simulation-secure FPSI**. Only the no-SBB EMP run (survey Table 3) is exact. |
| Balanced d=128, δ=4, n=2^8 | Requires code changes: a 128-bit input parser, and either a loop over 2^8 queries or one circuit of 2^8 × 2^8 comparisons. It also still yields only a membership bit per query, not the FPSI output. |
| Rough cost | The paper's EMP number is ≈ 1.8 ms and ≈ 186 KiB per 256-bit pair (2^16 pairs: 117.5 s, 12.2 GB). At 128 bits, ×2^16 pairs, that is **≈ 60–120 s and ≈ 6 GB**. The circuit is 16-bit-integer heavy and not optimised. So the run would fit in 1 h of compute. |
| Why it was not run | (1) Loopback TCP is blocked in this session: any connect to 127.0.0.1/::1 times out (`../gao_ext/tcp_probe.py`), and EMP NetIO needs TCP. (2) The protocol is per-query, outputs only a membership bit and is not an FPSI, so a "balanced run" would be our own wrapper rather than their artifact. (3) It needs the EMP toolchain built through Docker. |
| Verdict | **Feasible only as a re-engineered generic-2PC baseline, not as the authors' protocol.** If needed, cite its Table 3 per-pair cost (≈ 1.8 ms and 186 KiB per pair, 256-bit) as a generic-GC reference point, as survey §12.3 already does. |

## 3. Additional finding: newer Lp FPSI with high-dimension support and public code

`L1` on {0,1}^d **is** Hamming distance. So high-dimensional L1 FPSI artifacts could in principle be run at d=128, δ=4. We did not run any of them, because they are out of this task's scope.

| Paper | Code | Notes |
|---|---|---|
| Hao, Yang, Chen, Zhang, Xue, Yang, Li, Deng, "Towards Scalable Fuzzy PSI via Efficient Fuzzy Matching" (arXiv 2608.11526, Aug 2026) | https://github.com/Th0masAndy/Scalable-FPSI (C++20, volePSI) | Has `-high` (high-dimensional) mode with `-p 1` (L1), `-d`, `-delta`, `-nn`. Inputs are synthetic integers; GCC 13+ is required. Its input assumptions for the high-dimension case were not checked. |
| Yang, Hao, Jia, Weng, Wen, Zhang, "Efficient Fuzzy PSI under One-Sided Assumptions" (arXiv 2608.17770, Aug/Sep 2026) | none found | Lp with O(log δ) cost under one-sided assumptions. |
| Dang et al. CCS'25 (ePrint 2025/1796) | https://github.com/zhouxv/ourFuzzyPSI-C | Survey §10c found no Hamming experiments. |

**Recommendation:** check whether a reviewer could count Scalable-FPSI `-high -p 1 -d 128 -delta 4` as a Hamming baseline. If it runs on binary inputs, it is a directly comparable public artifact that the survey does not yet list.

Sources:
- [ePrint 2024/330](https://eprint.iacr.org/2024/330)
- [sihangpu/fuzzy_PSI](https://github.com/sihangpu/fuzzy_PSI)
- [vegetable68/sbb](https://github.com/vegetable68/sbb)
- [ePrint 2025/911](https://eprint.iacr.org/2025/911)
- [arXiv 2608.11526](https://arxiv.org/abs/2608.11526)
- [Th0masAndy/Scalable-FPSI](https://github.com/Th0masAndy/Scalable-FPSI)
- [arXiv 2608.17770](https://arxiv.org/abs/2608.17770)
