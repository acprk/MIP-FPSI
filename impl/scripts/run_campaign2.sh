#!/usr/bin/env bash
# Final campaign (round 2): ONE consistent run of every number in the paper.
#  * all runs pinned: ours (two processes) on cores 38 and 39, otFPSI (two threads) on 38,39;
#  * every run of ours is verified against an exact plaintext index (FN/FP reported);
#  * repetitions interleaved; host load recorded per run.
# Usage: scripts/run_campaign2.sh   (Gao runs separately via scripts/run_gao.sh on cores 40,41)
set -uo pipefail
cd "$(dirname "$0")/.."
NB=./build3/net_bench
FB=./build3/fpsi_bench
OT=../baselines/others/xdup/01_otfpsi/build/otfpsi
# Guard: no other benchmark may share the cores (10/23: a stale run_vs_otfpsi.sh contaminated campaign2_20261023_1108).
if ps -C otfpsi,net_bench,fpsi_bench -o pid= | read -r _; then
  echo "another benchmark is running; refusing to start" >&2; exit 1
fi
OUT=../results/campaign2_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
{ echo "start=$(date)"; cat /proc/loadavg; git -C ../third_party/volepsi log --oneline -1; } > "$OUT/env.txt"
PORT=1400

ours() { # tag args...   (two processes, receiver verifies)
  local tag=$1; shift
  PORT=$((PORT + 1))
  taskset -c 39 $NB -role 1 -addr 127.0.0.1:$PORT -indexverify "$@" > "$OUT/$tag.recv.log" 2>&1 &
  local rp=$!
  sleep 0.3
  taskset -c 38 $NB -role 0 -addr 127.0.0.1:$PORT "$@" > "$OUT/$tag.send.log" 2>&1
  wait $rp
  echo "$tag load=$(cut -d' ' -f1 /proc/loadavg) $(grep -h -E 'wall_s|matches' "$OUT/$tag.recv.log" "$OUT/$tag.send.log" | tr '\n' ' ')" >> "$OUT/summary.txt"
}
otf() { # tag m n d tau
  local tag=$1 m=$2 n=$3 d=$4 t=$5
  /usr/bin/time -f "%e %U %S" -o "$OUT/$tag.time" taskset -c 38,39 $OT -proto_distance batched -proto_threshold np \
     -querier_size $m -responder_size $n -vector_size $d -threshold $t -prf aes -ot silent > "$OUT/$tag.log" 2>&1
  echo "$tag load=$(cut -d' ' -f1 /proc/loadavg) $(cat "$OUT/$tag.time") $(grep -h 'Total Communication' "$OUT/$tag.log" | head -1)" >> "$OUT/summary.txt"
}

# A. head-to-head with otFPSI, d=128, delta in {4,8,16}
for delta in 4 8 16; do
  case $delta in 4) BS="-b 5 -s 1";; 8) BS="-b 9 -s 1";; 16) BS="-b 9 -s 1";; esac
  for lg in 8 10 12; do n=$((1 << lg))
    for k in 1 2 3; do
      otf "ot_d128_t${delta}_2e${lg}_r$k" $n $n 128 $delta
      ours "ours_d128_t${delta}_2e${lg}_r$k" -m $n -n $n -d 128 -delta $delta $BS
    done
  done
done
for k in 1 2 3; do ours "ours_d128_t4_2e14_r$k" -m 16384 -n 16384 -d 128 -delta 4 -b 5 -s 1; done
otf "ot_d128_t4_2e14_r1" 16384 16384 128 4

# B. ablations at 2^12, d=128, delta=4
for k in 1 2 3; do
  ours "abl_nocompress_r$k" -m 4096 -n 4096 -nocompress
  ours "abl_uniqc_r$k" -m 4096 -n 4096 -uniqc
  ours "abl_pad44_r$k" -m 4096 -n 4096 -Bbar 4 -Ubar 4
  ours "abl_b3r1_r$k" -m 4096 -n 4096 -b 3 -s 1
  ours "abl_cover2_r$k" -m 4096 -n 4096 -cover 2
done

# C. scaling (one run each, verified)
for lg in 16 18 20; do n=$((1 << lg)); ours "scale_d128_t4_2e$lg" -m $n -n $n -d 128 -delta 4 -b 5 -s 1; done
for lg in 12 14 16 18; do n=$((1 << lg))
  ours "scale_d64_t3_b4s1_2e$lg" -m $n -n $n -d 64 -delta 3 -b 4 -s 1
  ours "scale_d64_t3_b6s3_2e$lg" -m $n -n $n -d 64 -delta 3 -b 6 -s 3
done

# D. real data: ours (full, UniqC-ablation, padded) and otFPSI at the same sizes (data-independent cost)
for k in 1 2 3; do
  ours "real_code_r$k" -data ../data/code_simhash64.bin -d 64 -delta 3 -b 4 -s 1
  ours "real_code_uniqc_r$k" -data ../data/code_simhash64.bin -d 64 -delta 3 -b 4 -s 1 -uniqc
done
ours "real_code_pad64" -data ../data/code_simhash64.bin -d 64 -delta 3 -b 4 -s 1 -Bbar 64 -Ubar 64
otf "ot_code" 2973 2959 64 3
ours "real_cifar" -data ../data/cifar_dhash64_mild.bin -d 64 -delta 4 -b 7 -s 3
ours "real_cifar_uniqc" -data ../data/cifar_dhash64_mild.bin -d 64 -delta 4 -b 7 -s 3 -uniqc
otf "ot_cifar" 9999 54994 64 4
echo "done=$(date)" >> "$OUT/env.txt"
