#!/bin/bash
# usage: run_batch.sh CONFIG_FILE
# CONFIG_FILE lines: VARIANT logN ELL T REP     (VARIANT = orig | fix)
# A run during which the user-1000 cgroup was throttled more than 100 times per
# minute of wall time is moved to logs_excluded/ and retried (at most 3
# attempts per config; the 3rd attempt is kept but flagged in logs/<tag>/FLAG).
ROOT=<ARTIFACT_ROOT>/baselines_r5/blass_noubir
while read -r VAR K ELL T REP; do
  case "$VAR" in ''|\#*) continue ;; esac
  if [ "$VAR" = "orig" ]; then BIN="$ROOT/fuzzyPSICode/src/build"; else BIN="$ROOT/fuzzyPSICode/src_fix/build"; fi
  TAG="${VAR}_k${K}_ell${ELL}_t${T}_r${REP}"
  if [ -f "$ROOT/logs/$TAG/summary.json" ]; then echo "skip $TAG"; continue; fi
  for ATT in 1 2 3; do
    rm -rf "$ROOT/logs/$TAG"
    echo "=== $TAG attempt $ATT start $(date -u +%T)"
    bash "$ROOT/run_one.sh" "$BIN" "$K" "$K" "$ELL" "$T" "$TAG" > "$ROOT/logs/$TAG.summary.out" 2>&1
    R=$(python3 "$ROOT/throttle_delta.py" "$ROOT/logs/$TAG/summary.json")
    echo "=== $TAG attempt $ATT done $(date -u +%T) throttled_per_min=$R"
    if [ "$R" -le 100 ]; then break; fi
    if [ "$ATT" -eq 3 ]; then echo "kept despite throttled_per_min=$R" > "$ROOT/logs/$TAG/FLAG"; break; fi
    mv "$ROOT/logs/$TAG" "$ROOT/logs_excluded/${TAG}_att${ATT}_throttled"
  done
done < "$1"
