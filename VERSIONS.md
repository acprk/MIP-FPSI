# Version chain: campaign -> binary -> source -> build label

All binaries were built on the measurement host (Ubuntu 22.04, GCC 11.4.0 `/usr/bin/c++`, CMake 4.4.3,
Release `-O3 -DNDEBUG -march=native`, flags in `impl/CMakeLists.txt`) against volePSI
`ec76012ed516e25d3f460af9b8680e1140a5d491` (unmodified; `deps/records/`). Binaries are not shipped;
the sha256 of every binary, static library and object file that still exists on the author's
machine is in `BINARIES_MANIFEST.tsv` / `BINARIES_OBJECTS.tsv`. The CMake cache of every build
directory is in `impl/build_records/<build>/CMakeCache.txt`.

"Recorded hash" is the sha256 the campaign wrote into its own `env.txt` when it started.
"On disk" says whether the binary still present in that build directory has the same hash.

## Main and supplementary tables

| Campaign directory | Paper item | Binary (recorded sha256) | On disk | Build dir / label | Source snapshot |
|---|---|---|---|---|---|
| `experiments_r6/e2_paired` | Main Table "paired" (53–115x, 13–27x), supp. Table "paired" | `audit_r5/build/fpsi_bench` `0f2b985b…1a55`; lib `libmipfpsi.a` `67172db0…3b3b` (= `impl/build11`, = `impl/build12`) | yes | build11 lineage (arbitrary-size Beneš networks, i.e. **build-B network code**, plus later fixes); not labelled in the paper | **shipped `impl/src` reproduces it**: `experiments_r6/harness/build.sh` recompiled `impl/src/main.cpp` against the build11 library and obtained `fpsi_bench_ref` with the same sha256 `0f2b985b…`; `impl/build12` (from the shipped sources) has the same `libmipfpsi.a` `67172db0…` |
| same | otFPSI side of E2 | `experiments_r6/otfpsi_inproc/otfpsi_inproc` `a26b2b33…c0b3` | yes | built by `experiments_r6/otfpsi_inproc/build.sh` from `baselines/others/xdup/01_otfpsi_cpu` (= xDup `01_otfpsi` + getrusage, `baselines/otfpsi_xdup/01_otfpsi_to_01_otfpsi_cpu.patch`) with the shim header `otfpsi_inproc/shim/coproto/Socket/AsioSocket.h` | xDup artifact, Zenodo 10.5281/zenodo.19480020 (`baselines/otfpsi_xdup/UPSTREAM_CHECK.txt`: local `01_otfpsi` = published zip, 26/26 files) |
| `experiments_r6/e1_attribution` | Main "Source of the gain" (358x/73x, 1.70x/1.42x), supp. "Attribution" | `audit_r5/build/fpsi_bench` `0f2b985b…`; `experiments_r6/harness/fpsi_bench_kc` `87fae11a…8b94` (main_kc.cpp = main.cpp + `-keycheck`) | yes | as E2 | as E2; `harness/main_kc.cpp` shipped |
| `experiments_r6/e3_otfpsi_real` | supp. "otFPSI on the exact real inputs" | `build_otfpsi/otfpsi` `1cc7b8ea…`, `build_stage2/or_deliver_local` `6796de01…` | yes | stage 2 links `impl/build10/libmipfpsi.a` | `e3_otfpsi_real/otfpsi_src`, `otfpsi_e3.patch`, `or_deliver_local.cpp` |
| `experiments_r6/e4_stress`, `e5_abort` | supp. "Receiver load, unbalanced sets", "Abort paths" | `impl/build12/fpsi_bench` `033a7a1a…`; lib `67172db0…` | yes | build12 | shipped `impl/src` (build12 was configured from it on 2026-11-12) |
| `experiments_r6/e6_candidates` | supp. Table "candidates" | `e6_candidates/build/cand_stats` `2640ed79…` (links build12 lib) | yes | build12 | `e6_candidates/cand_stats.cpp` |
| `results/campaign3_20261023_2103` | Main Table "vs otFPSI" + Fig. `vsot_cpu`, main Table "real data" (rows except DP+U-free fixed), supp. breakdown, comm table | `impl/build5/net_bench` `a558e673…ef5b`, `impl/build5/fpsi_bench` `8d028fe7…9a32`; otFPSI `01_otfpsi/build/otfpsi` `0c68aa5f…3373` | yes (all three) | **build A** (build5, power-of-two networks) | no snapshot of the build5 sources; `osn.cpp` was changed to arbitrary-size networks between build5 (2026-10-23 20:52) and build6 (2026-10-24 05:01), see the header of `impl/src/osn.h` |
| `results/c3b_extra_20261024_0116` | earlier five-data-set U-free/rounded runs (`impl/scripts/run_seed_noU.sh`, `run_c3b_extra.sh`), superseded by E2 in the paper | `impl/build5/net_bench` (script; hash not recorded in this directory) | – | build A | as campaign 3 |
| `results/campaign4_20261024_0922` | supp. "earlier TCP paired campaigns" (92–121x) | `impl/build6/net_bench` `a700a279…2f7c`; otFPSI `0c68aa5f…` | yes | **build B** (build6) | no snapshot |
| `results/campaign5_20261025_0013` | supp. build A/B ratios (1.01–1.05), common back end (1.51x/1.47x), CPython 5-round repetitions, CIFAR repetitions, delta=16 crossover | A = `build5/net_bench` `a558e673…`, B = `build6/net_bench` `a700a279…`, C = `build8/net_bench` `e3fb7016…bc90f`; otFPSI `01_otfpsi_cpu/build/otfpsi` `f232c639…b487` | A, B, C yes; **otFPSI no** (rebuilt 2026-10-27, now `8367adcb…`) | A = build A, B = build B, C = build B + error-path fixes + `-keycheck` | no snapshots |
| `results/dp_fixed_20261024_2356` | supp. Table "fixed-cover DP", main Table "real data" row "DP+U-free, fixed" | `impl/build7/net_bench` `e3a8e768…7c63` | yes | build B + error-path fixes (build7) | no snapshot |
| `results/campaign2_20261023_1344` | supp. Table "scale" (up to 2^20), supp. Table "ablations", block vs CoveringLSH | `impl/build3/net_bench`, `impl/build3/fpsi_bench` (script `run_campaign2.sh`; hash not recorded in `env.txt`) | – | **build3** (pre-build5; power-of-two networks; the paper calls the scaling runs "build A") | no snapshot |
| `results/policy_tests_20261025_0227` | supp. abort/policy tests (historical) | `impl/build9/net_bench` (script) | – | build9 | no snapshot |
| `experiments_r4/e1_otfpsi_matched` | supp. Table "output-matched otFPSI" | `impl/build10/net_bench` `33c4398e…`, `impl/build10/or_deliver` `0da26f86…`, otFPSI `8367adcb…` | yes | build10 | no snapshot (build10 = sources of 2026-10-27 08:15) |
| `experiments_r4/e3_wan` | supp. LAN/WAN table, main "Network" paragraph | `impl/build10/net_bench` `33c4398e…` | yes | build10 ("a later build of the same source with the proxy harness") | as above |
| `experiments_r4/e4_injection` | supp. content-level injection | `impl/build10/net_bench`, `impl/build11/net_bench` `e4d16199…` | yes | build10, build11 | build11 = shipped `impl/src` (library hash identical to build12) |
| `analysis/injection` | supp. load-injection analysis (DP+rounded columns) | Python only | – | – | shipped |

Not used by any number in the paper: `results/suite_20261022_1934` (impl/build), `final_20261022_2022`,
`vs_otfpsi_20261023_0955` (build2), `campaign2_20261023_1108` (build3; marked CONTAMINATED, no number
from it is used), `modes_r3_20261023_1956` (build5 validation), `policy_tests_20261024_2354/2355`,
`or_component_20261024`. `results/sender_clustered` feeds the supplement paragraph "Clustered sender
sets"; its binary was not recorded.

## Build labels used in the paper

- **Build A** = `impl/build5`: power-of-two switching networks (`osn.cpp` padded to 2^ceil(log N)).
- **Build B** = `impl/build6`: arbitrary-size Beneš-style networks (no padding). Every later build
  (7, 8, 9, 10, 11, 12, audit_r5) contains the build-B network code; only error-path fixes, flags
  (`-keycheck`, `-allpairs`) and harness code were added afterwards.

## Finding: the "five-data-set ranges" are not build A

Supplement, "Measurement details": "Build A (campaign 3, the five-data-set ranges and the breakdown)
uses power-of-two switching networks; build B (campaigns 4 and 5) uses arbitrary-size Beneš-style
networks". The five-data-set ranges now printed in the paper (main Table "paired", 53–115x and
13–27x; supplement Table "paired") come from `experiments_r6/e2_paired`, which ran the build11
library (`experiments_r6/e2_paired/RESULTS.md`, row "Ours"; `experiments_r6/env.txt`). That library
has the build-B network code (`impl/src/osn.h` lines 6–7, `impl/src/osn.cpp` line 143), and its
U-free traffic shows it: on the same five seeds, the build-A runs of `results/c3b_extra_20261024_0116`
send 110.7 / 231.3 MiB (delta=4, B=2 / B=4) and 1,175 / 559 MiB (delta=8, B=8 / B=4), the E2 runs
88.4 / 185 MiB and 849 / 403 MiB, i.e. the 20–28% reduction that the supplement attributes to build B
(campaign 5 gives 110.7 MiB for A and 88.4 MiB for B on the same input). The statement was correct for
the earlier five-data-set runs (`c3b_extra`, build A), which E2 replaced. The breakdown (`c3_break`,
campaign 3) and the main Table "vs otFPSI" are build A as stated. The supplement's "build A" for the
scaling table refers to campaign 2, which used build3 (an earlier power-of-two build).
