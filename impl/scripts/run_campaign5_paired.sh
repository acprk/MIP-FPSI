#!/usr/bin/env bash
# Campaign 5 (round-D request): paired, randomized rounds with protocol-scoped CPU for otFPSI.
#  * otFPSI_cpu = otFPSI artifact + getrusage around each interleaved protocol section
#    (excludes input generation and plaintext verification); SoftSpoken mode.
#  * real data (CPython size), the delta=16 crossover, and a controlled build A/B comparison
#    (build5 vs build6, same inputs, same round) at delta=4 and 8.
#  * 5 rounds; within each round the jobs run in a random order on the same two cores.
#  * CIFAR: our modes repeated 3 times (otFPSI CIFAR is reused from campaign 3: hours per run).
set -uo pipefail
cd "$(dirname "$0")/.."
if ps -C otfpsi,net_bench,fpsi_bench -o pid= | read -r _; then
  echo "another benchmark is running; refusing to start" >&2; exit 1
fi
A=./build5/net_bench
B=./build6/net_bench
C=./build8/net_bench   # build6 + error-path fixes + -keycheck ablation flag
OT=../baselines/others/xdup/01_otfpsi_cpu/build/otfpsi
OUT=../results/campaign5_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
{ echo "start=$(date)"; cat /proc/loadavg; sha256sum $A $B $C $OT; } > "$OUT/env.txt"
PORT=24000

ours() { # tag binary args...
  local tag=$1 nb=$2; shift 2
  PORT=$((PORT + 1))
  taskset -c 47 $nb -role 1 -addr 127.0.0.1:$PORT -indexverify "$@" > "$OUT/$tag.recv.log" 2>&1 &
  local rp=$!
  sleep 0.3
  taskset -c 46 $nb -role 0 -addr 127.0.0.1:$PORT "$@" > "$OUT/$tag.send.log" 2>&1
  local se=$?
  wait $rp; local re=$?
  echo "$tag sender_exit=$se receiver_exit=$re load=$(cut -d' ' -f1 /proc/loadavg)" >> "$OUT/summary.txt"
}
otf() { # tag m n d
  /usr/bin/time -f "%e %U %S" -o "$OUT/$1.time" taskset -c 46,47 $OT -proto_distance batched -proto_threshold np \
     -querier_size $2 -responder_size $3 -vector_size $4 -threshold 4 -prf aes -ot softspoken > "$OUT/$1.log" 2>&1
  echo "$1 exit=$? load=$(cut -d' ' -f1 /proc/loadavg)" >> "$OUT/summary.txt"
}
CODE="-data ../data/code_simhash64.bin -d 64 -delta 3 -b 4 -s 1"
S12="-m 4096 -n 4096 -d 128"

job() { # name -> command
  case $1 in
    ot12)        otf "ot12_r$k" 4096 4096 128 ;;
    otcode)      otf "otcode_r$k" 2973 2959 64 ;;
    code_exact)  ours "code_exact_r$k" $B $CODE ;;
    code_pow2)   ours "code_pow2_r$k" $B $CODE -pow2 ;;
    code_noU)    ours "code_noU_r$k" $B $CODE -pow2 -noU ;;
    t16_exact)   ours "t16_exact_r$k" $B $S12 -delta 16 -b 9 -s 1 ;;
    t16_pow2)    ours "t16_pow2_r$k" $B $S12 -delta 16 -b 9 -s 1 -pow2 ;;
    t4_pow2_A)   ours "t4_pow2_A_r$k" $A $S12 -delta 4 -b 5 -s 1 -pow2 ;;
    t4_pow2_B)   ours "t4_pow2_B_r$k" $B $S12 -delta 4 -b 5 -s 1 -pow2 ;;
    t4_noU_A)    ours "t4_noU_A_r$k" $A $S12 -delta 4 -b 5 -s 1 -pow2 -noU ;;
    t4_noU_B)    ours "t4_noU_B_r$k" $B $S12 -delta 4 -b 5 -s 1 -pow2 -noU ;;
    t8_pow2_A)   ours "t8_pow2_A_r$k" $A $S12 -delta 8 -b 9 -s 1 -pow2 ;;
    t8_pow2_B)   ours "t8_pow2_B_r$k" $B $S12 -delta 8 -b 9 -s 1 -pow2 ;;
    # common-backend control: same cover and counters, full payload, conventional key check
    t4_ctrl)     ours "t4_ctrl_r$k" $C $S12 -delta 4 -b 5 -s 1 -pow2 -nocompress -keycheck ;;
    t4_ours8)    ours "t4_ours8_r$k" $C $S12 -delta 4 -b 5 -s 1 -pow2 ;;
    t8_ctrl)     ours "t8_ctrl_r$k" $C $S12 -delta 8 -b 9 -s 1 -pow2 -nocompress -keycheck ;;
    t8_ours8)    ours "t8_ours8_r$k" $C $S12 -delta 8 -b 9 -s 1 -pow2 ;;
  esac
}
JOBS="ot12 otcode code_exact code_pow2 code_noU t16_exact t16_pow2 t4_pow2_A t4_pow2_B t4_noU_A t4_noU_B t8_pow2_A t8_pow2_B t4_ctrl t4_ours8 t8_ctrl t8_ours8"
for k in 1 2 3 4 5; do
  order=$(printf '%s\n' $JOBS | shuf)
  echo "round $k order: $(echo $order)" >> "$OUT/env.txt"
  for j in $order; do job "$j"; done
done
CIFAR="-data ../data/cifar_dhash64_mild.bin -d 64 -delta 4 -b 6 -s 2"
for k in 1 2 3; do
  ours "cifar_exact_r$k" $B $CIFAR
  ours "cifar_pow2_r$k" $B $CIFAR -pow2
  ours "cifar_noU_r$k" $B $CIFAR -pow2 -noU
done
echo "done=$(date)" >> "$OUT/env.txt"
echo "$OUT"
