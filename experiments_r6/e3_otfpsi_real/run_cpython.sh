#!/usr/bin/env bash
# CPython block of E3: 3 rounds, each in random order: native otFPSI on the real deduplicated
# fingerprints, the same real-input code path on size-matched uniformly random fingerprints, and the
# artifact's own native benchmark (random data generated inside). SoftSpoken, delta=3, cores 42,43.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
for k in 1 2 3; do
  for j in $(printf '%s\n' real rand art | shuf); do
    case $j in
      real) bash "$HERE/run_e3.sh" native "code_real_soft_r$k" "$HERE/code_dedup.bin" 3 softspoken ;;
      rand) bash "$HERE/run_e3.sh" native "code_rand_soft_r$k" "$HERE/code_random.bin" 3 softspoken ;;
      art)  bash "$HERE/run_e3.sh" artifact "code_art_soft_r$k" 2973 2959 64 3 softspoken ;;
    esac
    echo "round $k job $j done $(date -Is)" >> "$HERE/results/cpython_order.txt"
  done
done
