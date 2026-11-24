#!/usr/bin/env bash
# Campaign 3 (round-3 revision). One binary (build5) for every run of ours.
#  * primary metric: CPU seconds per party (robust to cgroup throttling on this shared host);
#    wall time, load and nr_throttled delta are logged per run as secondary data
#  * otFPSI in both OT modes (silent = paper config, softspoken = artifact default)
#  * ours in four leakage modes: exact (L_S=B, L_R=U), pow2 (ceil log2), noU+pow2, dp(eps=1)+pow2
#  * runs interleaved per repetition; every run of ours verified against the exact plaintext result
# Usage: scripts/run_campaign3.sh
set -uo pipefail
cd "$(dirname "$0")/.."
if ps -C otfpsi,net_bench,fpsi_bench -o pid= | read -r _; then
  echo "another benchmark is running; refusing to start" >&2; exit 1
fi
NB=./build5/net_bench
FB=./build5/fpsi_bench
OT=../baselines/others/xdup/01_otfpsi/build/otfpsi
CG=/sys/fs/cgroup/user.slice/user-1000.slice/cpu.stat
OUT=../results/campaign3_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
{ echo "start=$(date)"; cat /proc/loadavg; git -C ../third_party/volepsi log --oneline -1; sha256sum $NB $FB $OT; } > "$OUT/env.txt"
PORT=2000
gate() { while [ "$(cut -d' ' -f1 /proc/loadavg | cut -d. -f1)" -ge 30 ]; do sleep 60; done; }
thr() { grep nr_throttled "$CG" | cut -d' ' -f2; }

ours() { # tag args...
  local tag=$1; shift
  gate; local t0; t0=$(thr)
  PORT=$((PORT + 1))
  taskset -c 39 $NB -role 1 -addr 127.0.0.1:$PORT -indexverify "$@" > "$OUT/$tag.recv.log" 2>&1 &
  local rp=$!
  sleep 0.3
  taskset -c 38 $NB -role 0 -addr 127.0.0.1:$PORT "$@" > "$OUT/$tag.send.log" 2>&1
  wait $rp
  echo "$tag load=$(cut -d' ' -f1 /proc/loadavg) thr=$(( $(thr) - t0 )) $(grep -h -E 'wall_s|matches' "$OUT/$tag.recv.log" "$OUT/$tag.send.log" | tr '\n' ' ')" >> "$OUT/summary.txt"
}
otf() { # tag m n d otmode
  local tag=$1 m=$2 n=$3 d=$4 om=$5
  gate; local t0; t0=$(thr)
  /usr/bin/time -f "%e %U %S" -o "$OUT/$tag.time" taskset -c 38,39 $OT -proto_distance batched -proto_threshold np \
     -querier_size $m -responder_size $n -vector_size $d -threshold 4 -prf aes -ot $om > "$OUT/$tag.log" 2>&1
  echo "$tag load=$(cut -d' ' -f1 /proc/loadavg) thr=$(( $(thr) - t0 )) $(cat "$OUT/$tag.time") $(grep -h 'Total Communication' "$OUT/$tag.log" | head -1)" >> "$OUT/summary.txt"
}
modes() { # prefix reps args...   (four leakage modes, interleaved)
  local pre=$1 reps=$2; shift 2
  for k in $(seq 1 $reps); do
    ours "${pre}_exact_r$k" "$@"
    ours "${pre}_pow2_r$k" "$@" -pow2
    ours "${pre}_noU_r$k" "$@" -pow2 -noU
    ours "${pre}_dp_r$k" "$@" -pow2 -dpeps 1
  done
}

# A. otFPSI (delta-independent), both OT modes, interleaved
for k in 1 2 3 4 5; do
  for lg in 8 10 12; do n=$((1 << lg))
    otf "ot_silent_2e${lg}_r$k" $n $n 128 silent
    otf "ot_soft_2e${lg}_r$k" $n $n 128 softspoken
  done
done

# B. ours, d=128, four modes
for lg in 8 10 12; do n=$((1 << lg))
  modes "d128_t4_2e$lg" 5 -m $n -n $n -d 128 -delta 4 -b 5 -s 1
  modes "d128_t8_2e$lg" 5 -m $n -n $n -d 128 -delta 8 -b 9 -s 1
done
for lg in 8 10 12; do n=$((1 << lg))
  modes "d128_t16_2e$lg" 3 -m $n -n $n -d 128 -delta 16 -b 9 -s 1
done

# C. real data (covers from build5/choose_cover: CPython (4,1), CIFAR (6,2) in both exact and pow2)
modes "real_code" 3 -data ../data/code_simhash64.bin -d 64 -delta 3 -b 4 -s 1
for k in 1 2 3; do otf "ot_code_silent_r$k" 2973 2959 64 silent; otf "ot_code_soft_r$k" 2973 2959 64 softspoken; done
modes "real_cifar" 1 -data ../data/cifar_dhash64_mild.bin -d 64 -delta 4 -b 6 -s 2

# D. cost breakdown and covers
for k in 1 2 3; do
  gate; t0=$(thr)
  taskset -c 38 $FB -m 16384 -n 16384 -d 128 -delta 4 -b 5 -s 1 -v > "$OUT/break_2e14_r$k.log" 2>&1
  echo "break_2e14_r$k load=$(cut -d' ' -f1 /proc/loadavg) thr=$(( $(thr) - t0 ))" >> "$OUT/summary.txt"
  ours "cov4_cover2_r$k" -m 4096 -n 4096 -d 128 -delta 4 -cover 2
  ours "cov16_cover3_r$k" -m 4096 -n 4096 -d 128 -delta 16 -cover 3
done

# E. large sizes (one run each)
for md in exact pow2 noU; do
  f=""; [ $md = pow2 ] && f="-pow2"; [ $md = noU ] && f="-pow2 -noU"
  ours "d128_t4_2e14_${md}_r1" -m 16384 -n 16384 -d 128 -delta 4 -b 5 -s 1 $f
done
otf "ot_soft_2e14_r1" 16384 16384 128 softspoken
otf "ot_soft_cifar_r1" 9999 54994 64 softspoken
echo "done=$(date)" >> "$OUT/env.txt"
