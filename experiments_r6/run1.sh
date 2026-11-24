#!/usr/bin/env bash
# run1.sh <log> <timeout_s> <fpsi_bench args...>
# One fpsi_bench run pinned to cores 44,45 under /usr/bin/time -v; records loadavg and the
# user-slice cgroup nr_throttled before and after. Prints the exit code of fpsi_bench.
FB=<ARTIFACT_ROOT>/impl/build12/fpsi_bench
CG=/sys/fs/cgroup/user.slice/user-1000.slice/cpu.stat
log=$1; to=$2; shift 2
thr0=$(grep nr_throttled "$CG" | cut -d' ' -f2)
la0=$(cut -d' ' -f1-3 /proc/loadavg)
{
  echo "# cmd: $FB $*"
  echo "# env MIP_INJECT_CUCKOO_FAIL=${MIP_INJECT_CUCKOO_FAIL:-}"
  echo "# start $(date -Is) loadavg_before=$la0 nr_throttled_before=$thr0"
} > "$log"
/usr/bin/time -v timeout "$to" taskset -c 44,45 "$FB" "$@" >> "$log" 2>&1
ec=$?
thr1=$(grep nr_throttled "$CG" | cut -d' ' -f2)
la1=$(cut -d' ' -f1-3 /proc/loadavg)
echo "# end $(date -Is) exit=$ec loadavg_after=$la1 nr_throttled_after=$thr1 nr_throttled_delta=$((thr1 - thr0))" >> "$log"
echo "$ec"
