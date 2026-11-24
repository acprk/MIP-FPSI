#!/usr/bin/env bash
# Small-scale correctness self-test (a few seconds after the build).
#
#   ./selftest.sh [BUILD_DIR]        (default BUILD_DIR = ./build)
#
# Builds impl/ (needs the pinned dependencies, see README.md "Build"), then runs
#   1. unit_tests (switching-network routing for 11 sizes incl. non-powers of two, covers, circuits, ...)
#   2. fpsi_bench with both parties in one process at m=n=2^8, d=128, delta=4, cover (5,1), in the
#      exact, rounded (-pow2) and U-free (-pow2 -noU) modes, each checked against an all-pairs
#      plaintext computation (fpsi_bench exits 2 on any false negative/positive, 3 on an abort).
# Extra cmake arguments can be passed in CMAKE_ARGS, e.g.
#   CMAKE_ARGS="-DCMAKE_CXX_COMPILER=g++-11 -DVOLEPSI_ROOT=/opt/volepsi" ./selftest.sh
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd)
B=${1:-$A/build}
# shellcheck disable=SC2086
cmake -S "$A/impl" -B "$B" -DCMAKE_BUILD_TYPE=Release ${CMAKE_ARGS:-}
cmake --build "$B" -j "$(nproc)" --target fpsi_bench unit_tests
"$B/unit_tests"
for mode in "" "-pow2" "-pow2 -noU"; do
  for seed in 1 2; do
    echo "== fpsi_bench m=n=256 delta=4 seed=$seed ${mode:-exact}"
    # shellcheck disable=SC2086
    "$B/fpsi_bench" -m 256 -n 256 -d 128 -delta 4 -b 5 -s 1 -nt 1 -seed "$seed" $mode | tee "$B/selftest.out"
    grep -q "false_neg=0 false_pos=0" "$B/selftest.out"
  done
done
echo "selftest passed"
