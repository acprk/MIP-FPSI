# MIP-FPSI artifact
Code, data-set builders, raw logs and table generators for "Hamming fuzzy PSI" (MIP-FPSI).
Public location: <https://github.com/acprk/MIP-FPSI>. Archive integrity: `SHA256SUMS.txt` (one line per file).
Extended supplement (all measurement details, tables S1–S10 of the long version): `docs/extended_supplement.pdf`.

Quick start, no build needed:

    ./reproduce_tables.sh            # regenerates the paper's tables and paired ratios from the shipped logs (python3 only, < 1 min)

Build and small correctness test (needs the pinned dependencies, see "Build"):

    deps/fetch_volepsi.sh && ./selftest.sh

## Contents

| Path | What |
|---|---|
| `impl/src`, `impl/CMakeLists.txt` | the protocol, C++20 on volePSI/libOTe (~1,900 lines), benchmark drivers `fpsi_bench` (both parties in one process) and `net_bench` (two processes over TCP), `unit_tests` |
| `impl/scripts` | campaign drivers of the TCP campaigns (`run_campaign3.sh`, `run_campaign5_paired.sh`, `run_dp_fixed.sh`, ...) |
| `impl/build_records` | `CMakeCache.txt` of every build directory used (build, build2 ... build12, audit_r5 build) |
| `deps/` | `fetch_volepsi.sh` (clone + build pinned volePSI and its dependencies), `record_dep_versions.sh`, `records/` (CMake cache and generated config of the volePSI build that was linked) |
| `results/` | raw logs of the TCP campaigns (campaign2 ... campaign5, fixed-cover DP, policy tests, ...) |
| `experiments_r4/` | output-matched otFPSI, semantic audit, LAN/WAN emulation, content-level injection |
| `experiments_r6/` | paired campaign E2 (main Table "paired"), attribution E1, otFPSI on real inputs E3, load stress E4, abort tests E5, candidate statistics E6: job lists, runner, aggregation, environment record (`env.txt`), raw per-run logs (`*/runs/`), in-process otFPSI shim and build script (`otfpsi_inproc/`), key-check harness (`harness/`) |
| `analysis/injection/` | load-injection analysis (scripts, per-trial CSVs, `RESULTS.md`, `make_tables.py`) |
| `audit_r5/` | independent audit: adversarial correctness sweep, wire audit, policy tests |
| `baselines/`, `baselines_r5/` | ball expansion, Gao et al., Blass–Noubir, Hao et al., per-query and survey runs; `baselines/otfpsi_xdup/` records the otFPSI (xDup) artifact used and our getrusage patch |
| `data/` | data-set builders (CPython SimHash-64, CIFAR-10 dHash-64) and the fingerprint files used |
| `round4/` | checks for the theorems: irredundance MILP/SAT, end-to-end tests, gap numbers, and `thm3c_cert.py`, the certificate for the load intervals of Theorem 3(c) |
| `paper/` | table generators (`make_*.py`, `shade_tables.py`), the generated table bodies used by the paper (`tables_reference/`), expected outputs (`expected/`), figure script `plot_vsot.py` |
| `tools/` | `jsonl_until.py` (see E1 below) |
| `VERSIONS.md` | campaign -> binary sha256 -> build directory -> build label, and what source each binary corresponds to |
| `BINARIES_MANIFEST.tsv`, `BINARIES_OBJECTS.tsv` | sha256 of every executable, static library and object file of the campaigns (binaries are not shipped) |
| `SKIPPED_LARGE.txt` | files over 20 MiB left out of the shipped directories (none in this version; raw downloads: see limitation 8) |

## Hardware and resources

The measurements ran on one shared host: 2x Intel Xeon Gold 6230R (52 cores, 104 threads), Ubuntu
22.04 (kernel 6.8), user cgroup limited to 26 CPUs. Each run was pinned with `taskset` to two
physical cores (one per party) with their hyper-threads left idle; the primary metric is the CPU
time of both parties (`getrusage` around the protocol and `/usr/bin/time -v`). Wall times are
inflated by the shared host.

- CPU: x86-64 with AVX2, AES-NI, PCLMUL, SSE4.1 (the code is built with `-march=native`).
- Memory per run (peak RSS, both parties): E2 paired campaign <= 3.8 GiB for ours and 6.4 GiB for
  otFPSI (silent OT); fixed-cover DP up to 12.1 GiB per party; the E4 stress run with load B = 512
  used 45.7 GiB in exact mode. Plan for 64 GiB to rerun everything.
- `reproduce_tables.sh`: python3 >= 3.8, standard library only, about 1 minute, no network.

## Dependency pinning

| Component | Version |
|---|---|
| volePSI | https://github.com/Visa-Research/volepsi.git, commit `ec76012ed516e25d3f460af9b8680e1140a5d491` ("Update README.md", 2025-09-02), unmodified (`git status` clean); no git submodules |
| libOTe | pinned by volePSI `thirdparty/getLibOTe.cmake`: https://github.com/osu-crypto/libOTe.git `d55867114c78272be7142bd67ebdcb346fec8621` (libOTe pins cryptoTools, coproto, macoro, libsodium, boost, bitpolymul in turn) |
| volePSI build options (from `deps/records/volepsi_CMakeCache.txt`) | `CMAKE_BUILD_TYPE=Release`, `VOLE_PSI_ENABLE_SSE=ON`, `VOLE_PSI_ENABLE_BOOST=ON`, `VOLE_PSI_ENABLE_SODIUM=ON`, `VOLE_PSI_SODIUM_MONTGOMERY=ON`, `VOLE_PSI_ENABLE_BITPOLYMUL=ON`, `VOLE_PSI_ENABLE_GMW=ON`, `VOLE_PSI_ENABLE_CPSI=ON`, `VOLE_PSI_ENABLE_OPPRF=ON`, `VOLE_PSI_ENABLE_OPENSSL=OFF`, `VOLE_PSI_ENABLE_RELIC=OFF`, `VOLE_PSI_ENABLE_PIC=OFF`, `VOLE_PSI_ENABLE_ASAN=OFF`, `VOLE_PSI_NO_SYSTEM_PATH=true`, `FETCH_AUTO=OFF` (dependencies taken from an existing install prefix) |
| Compiler / tools | GCC 11.4.0 (`/usr/bin/c++`), CMake 4.4.3, Python 3 |
| otFPSI baseline | xDup artifact v1.0.0, Zenodo DOI 10.5281/zenodo.19480020 (`xdup-artifact.zip`, md5 `fef9cced0204de1dc97a168004262745`); our only change is `getrusage` around its two protocol sections (`baselines/otfpsi_xdup/01_otfpsi_to_01_otfpsi_cpu.patch`) |

The exact commits of libOTe/cryptoTools/coproto/macoro in the install prefix used for the timed builds
were not recorded with the experiments (see "Known limitations"). `deps/fetch_volepsi.sh` builds the
versions pinned by volePSI `ec76012`.

## Build

    deps/fetch_volepsi.sh                     # clones volePSI @ ec76012 into deps/volepsi and builds it and its dependencies (network access, ~20-40 min)
    cmake -S impl -B build -DCMAKE_CXX_COMPILER=g++-11
    cmake --build build -j

`impl/CMakeLists.txt` takes three cache variables with these defaults:
`VOLEPSI_ROOT=deps/volepsi`, `VOLEPSI_BUILD=$VOLEPSI_ROOT/out/build/linux`,
`VOLEPSI_DEPS_PREFIX=$VOLEPSI_ROOT/out/install/linux` (the old names `VOLEPSI_SRC`, `VOLEPSI_INSTALL`
are accepted). To use an existing volePSI build, pass them explicitly. Configuration stops with a
message if any of them is missing. Use GCC 11 or newer (C++20 coroutines).

## Self-test

    ./selftest.sh                # or: CMAKE_ARGS="-DCMAKE_CXX_COMPILER=g++-11" ./selftest.sh

builds `fpsi_bench` and `unit_tests`, runs `unit_tests`, then runs `fpsi_bench -m 256 -n 256 -d 128
-delta 4 -b 5 -s 1 -nt 1 -seed {1,2}` in the exact, rounded (`-pow2`) and U-free (`-pow2 -noU`) modes.
Each run compares the protocol output with an all-pairs plaintext computation and prints
`false_neg=0 false_pos=0`; `fpsi_bench` exits with status 2 on any false negative or positive and 3 on an
abort. A single run by hand:

    build/fpsi_bench -m 256 -n 256 -d 128 -delta 4 -b 5 -s 1 -nt 1 -seed 1

What was verified when this archive was assembled: `impl/CMakeLists.txt` configures against the
in-tree volePSI build and stops with the documented message when a dependency is missing; the shell
scripts pass `bash -n`; `reproduce_tables.sh` passes (below). The self-test itself was not built or
run while packaging (the dependency prefix is on the measurement host). The binaries built from these
sources ran the audit in `audit_r5/REPORT.txt`: `unit_tests` exit 0, and a correctness sweep of 1,140
`fpsi_bench` runs in which 1,132 were exact and 8 runs of an ablation flag (`-otthreshold` at d=64,
delta=8) aborted by design without output.

## Reproducing the tables without rerunning benchmarks

`./reproduce_tables.sh [OUTDIR]` regenerates, from the shipped logs only, and compares with the shipped
or paper versions:

| Paper item | Campaign directory | Generator | Check |
|---|---|---|---|
| Main Table "CPU time against otFPSI" and Fig. `vsot_cpu` (data `paper/expected/vsot_cpu.csv`), main Table "real data" (except the fixed-cover DP row), supp. breakdown and communication | `results/campaign3_20261023_2103` (build A) | `paper/make_c3_tables.py`, `paper/shade_tables.py` | byte-identical to `paper/tables_reference/c3_*.tex` |
| Supp. Tables "scaling" and "ablations", block vs CoveringLSH | `results/campaign2_20261023_1344` | `paper/make_campaign_tables.py` | byte-identical to `paper/tables_reference/c2_*.tex` |
| Main Table "paired" (U-free 53–115x at delta=4, 13–27x at delta=8), supp. Table "paired" | `experiments_r6/e2_paired` | `experiments_r6/aggregate.py e2`, `experiments_r6/paired_exact.py` | `tables.md` and `results.csv` byte-identical; exact medians, e.g. exact delta=4 80.55x, U-free delta=4 52.74–114.49x, delta=8 12.82–27.23x |
| Main "Source of the gain" (358x, 73x, 1.70x, 1.42x), supp. "Attribution" | `experiments_r6/e1_attribution` | `tools/jsonl_until.py`, `experiments_r6/aggregate.py e1` | snapshot of 2026-11-12 09:10:03 reproduced (see limitations) |
| Supp. build A/B ratios (1.01–1.05), common back end (1.51x, 1.47x), CPython and CIFAR repetitions | `results/campaign5_20261025_0013` | `paper/make_c5_paired.py` | equal to `paper/expected/c5.txt` |
| Supp. earlier paired TCP campaign | `results/campaign4_20261024_0922` | `paper/make_c4_paired.py` | equal to `paper/expected/c4.txt` |
| Supp. load injection (incl. DP+rounded columns) | `analysis/injection/results` | `analysis/injection/make_tables.py` | `tables.md` byte-identical |

Items whose numbers were copied by hand from a campaign directory, without a generator:
supp. Table "fixed-cover DP" and the "DP+U-free, fixed" row of the main Table "real data"
(`results/dp_fixed_20261024_2356`: `summary.txt`, `*.time`), supp. Table "output-matched otFPSI"
(`experiments_r4/e1_otfpsi_matched/RESULTS.md`), LAN/WAN (`experiments_r4/e3_wan/RESULTS.md`), E3–E6
(`experiments_r6/e*/RESULTS.md`), Theorem 3(c) load intervals (`python3 round4/thm3c_cert.py`).
`paper/plot_vsot.py` draws the figure from `tables/c3_vsot.tex`; it imports a plotting-style module
that is not part of this archive, so `reproduce_tables.sh` checks the table, not the PDF.

## Rerunning the campaigns

The drivers are kept exactly as they were run, including absolute paths of the measurement host
(`<ARTIFACT_ROOT>/...`) and build directory names (`impl/buildN`); edit the
variables at the top of a script before rerunning. `VERSIONS.md` lists which build each campaign used;
all builds after build6 share the network code of the shipped sources. Approximate durations on the
host above: E2 paired campaign 1 h (`experiments_r6/run_all.sh`, then E1 another 1.7 h); campaign 3
5.7 h; campaign 5 2.2 h; campaign 4 0.5 h; fixed-cover DP 16 min; output-matched otFPSI 1.6 h; LAN/WAN
2.2 h; the 2^20 scaling run 31 min by itself.

Memory cap. The fixed-cover DP driver `impl/scripts/run_dp_fixed.sh` sets `LIMIT_KB=$((40 * 1024 *
1024))`, i.e. 41,943,040 KiB = 42,949,672,960 bytes (40 GiB), and compares it every 2 s with the sum of
the resident set sizes of the two party processes (`ps -o rss= --ppid <sender>,<receiver>`); it is a
limit for both parties together, not per party. It stopped `code_eps01_r1` and `syn12_eps01_r1`
(`results/dp_fixed_20261024_2356/summary.txt`, `killed_mem=1`). The script comment says "40 GB".
No memory limit was enforced in campaign 3: the delta=16, 2^12 U-free and DP runs were cancelled by
command line (`impl/scripts/skip_heavy_c3.sh`, `results/campaign3_20261023_2103/skipped.txt`) on an
estimate of more than 40 GB per party. The E4 stress runs use `prlimit --as=107374182400` (100 GiB
of address space for the single process that holds both parties).

## Known limitations

1. Only the final sources are shipped (`impl/src`). They are the sources of build11/build12, which
   produced the binaries of E1, E2, E4–E6 and the audit (the library rebuilds bit-identically, see
   `VERSIONS.md`). Builds 2–10, including build A (build5) and build B (build6), have no source
   snapshot; their binaries' sha256 values match the hashes the campaigns recorded, and the changes
   between builds are described in `impl/MODES_R3.md`, the header of `impl/src/osn.h` and the
   comments of the drivers.
2. The dependency libraries of the timed builds came from an existing install prefix on the host whose
   commits were not recorded. `deps/fetch_volepsi.sh` rebuilds the versions pinned by volePSI
   `ec76012`, so rebuilt binaries will not be bit-identical and timings can differ. Running
   `deps/record_dep_versions.sh` on the host would close this gap.
3. The otFPSI binary of campaign 5 (sha256 `f232c639…`) was rebuilt later from the same directory
   and no longer exists; the current one is `8367adcb…`.
4. E1: `experiments_r6/e1_attribution/tables.md`, `results.csv` and the attribution numbers in the
   paper were aggregated at 09:10:03 PST while the runner was still adding rounds (last run ended
   09:18:55). `reproduce_tables.sh` reproduces that snapshot and also writes the table over all
   44 records, whose medians differ (e.g. all-pairs 361x instead of 358x, key check 1.63x/1.44x instead
   of 1.70x/1.42x at delta=4/8).
5. In round 6 loopback TCP was unavailable on the host, so E1–E6 run both parties in one process over
   an in-memory channel, and native otFPSI was linked against coproto's in-memory socket
   (`experiments_r6/otfpsi_inproc/`); byte counts match its TCP runs. This removes TCP system-call
   cost, which favours otFPSI.
6. The host was shared and under a cgroup CPU quota. Runs were pinned and throttling was recorded
   (`nr_throttled`); wall times are inflated, which is why CPU time is the primary metric.
7. The paper prints the paired ratios as integers rounded from one-decimal values; the unrounded values
   are printed by `experiments_r6/paired_exact.py` (step 3 of `reproduce_tables.sh`).
8. Not included (public sources, or available on request): the raw downloads used by the data-set
   builders (`Python-3.11.9.tgz`, `Python-3.12.4.tgz` from python.org, `cifar-10-python.tar.gz` from
   the CIFAR-10 site, 26–170 MB each), all binaries, and the build trees of the third-party baselines.


## Note on paths

For anonymous release, local absolute paths in scripts, logs and build records were replaced by the placeholders `<ARTIFACT_ROOT>` (this directory), `<VOLEPSI_ROOT>` (the pinned volePSI checkout), `<DEPS_ROOT>`, `<HOME>` and `<TMP>`. No other byte of any log, CSV or result was changed. Set these to your own paths before rerunning a script.
