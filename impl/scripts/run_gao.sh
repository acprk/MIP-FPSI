#!/usr/bin/env bash
# Re-run the public artifact of Gao et al. (ASIACRYPT'24, github.com/ql70ql70/Fuzzy-Private-Set-
# Intersection-from-Fuzzy-Mapping) in its Hamming benchmark setting d=128, delta=4, pinned to two
# dedicated physical cores. Usage: scripts/run_gao.sh [CORES] [LOG2 sizes...]
set -euo pipefail
CORES="${1:-40,41}"; shift || true
SIZES="${*:-8 10 12}"
GAO=<ARTIFACT_ROOT>/../code_repro/Fuzzy-Private-Set-Intersection-from-Fuzzy-Mapping/build/main
OUT="$(cd "$(dirname "$0")/.." && pwd)/../baselines/gao_pinned_$(date +%Y%m%d_%H%M)"
mkdir -p "$OUT"
{ echo "cores=$CORES host=$(hostname) start=$(date)"; echo "binary=$GAO"; cat /proc/loadavg;
  git -C "$(dirname "$GAO")/.." log --oneline -1 2>/dev/null || true; } > "$OUT/env.txt"
for lg in $SIZES; do
  echo "== m=n=2^$lg start=$(date) load=$(cat /proc/loadavg)" >> "$OUT/env.txt"
  /usr/bin/time -v taskset -c "$CORES" "$GAO" -fpsi -t13 -hams "$lg" -hamr "$lg" -hamdelta 4 -hamd 128 -hami 6 \
    > "$OUT/gao_m2e${lg}_n2e${lg}.log" 2> "$OUT/gao_m2e${lg}_n2e${lg}.time"
done
echo "done=$(date)" >> "$OUT/env.txt"
