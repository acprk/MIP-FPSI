#!/usr/bin/env bash
# Watcher for campaign3: cancels the delta=16, 2^12 runs in U-free and DP mode (estimated >40 GB per
# party for the extended permutation, hours for DP; delta=16 is already outside the efficient regime).
# Logs every cancellation to the campaign directory. Exits when campaign3 writes done=.
OUT=$1
while ! grep -q "^done=" "$OUT/env.txt"; do
  for pid in $(ps -C net_bench -o pid=); do
    args=$(tr '\0' ' ' < /proc/$pid/cmdline 2>/dev/null)
    case "$args" in
      *"-m 4096 -n 4096 -d 128 -delta 16"*"-noU"*|*"-m 4096 -n 4096 -d 128 -delta 16"*"-dpeps"*)
        kill "$pid" && echo "skipped $(date): $args" >> "$OUT/skipped.txt" ;;
    esac
  done
  sleep 20
done
