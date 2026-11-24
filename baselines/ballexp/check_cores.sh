#!/bin/bash
# List running threads currently placed on cores 40,41,92,93 (excluding idle kernel threads),
# plus the processes whose affinity is pinned to those cores.
ps -eLo pid,psr,stat,pcpu,comm | awk 'NR==1 || (($2==40||$2==41||$2==92||$2==93) && $3 ~ /R/)'
echo "--- pinned gcn_pipe / bench affinities"
for p in $(ps -eo pid,comm | awk '$2=="gcn_pipe"{print $1}'); do taskset -cp $p; done
grep MemAvailable /proc/meminfo
