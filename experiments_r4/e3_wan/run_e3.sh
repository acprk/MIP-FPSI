#!/usr/bin/env bash
# E3: wall-clock latency under an emulated WAN (100 Mbps per direction, 40 ms one-way, i.e. 80 ms RTT)
# and a fast LAN (1 Gbps, 0.5 ms one-way), via a user-space proxy on the path between the parties.
# Ours (exact / rounded / U-free) at 2^12, d=128, delta=4 and on CPython; otFPSI (silent OT, its
# lowest-communication mode, and SoftSpoken) on the same sizes. 3 repetitions, interleaved.
set -uo pipefail
cd "$(dirname "$0")"
if ps -C otfpsi,net_bench,fpsi_bench,or_deliver -o pid= | read -r _; then
  echo "another benchmark is running; refusing to start" >&2; exit 1
fi
NB=../../impl/build10/net_bench
OT=../../baselines/others/xdup/01_otfpsi_wan/build/otfpsi
PY=/usr/bin/python3
OUT=results_$(date +%Y%m%d_%H%M)
mkdir -p "$OUT"
python3 ../e1_otfpsi_matched/gen_synth.py 4096 4096 128 4 12 "$OUT/syn12.bin"
CODE=../../data/code_simhash64.bin
{ echo "start=$(date)"; cat /proc/loadavg; sha256sum $NB $OT "$OUT/syn12.bin" $CODE; } > "$OUT/env.txt"
PORT=27000
proxy_start() { # mbps delay_ms upstream_port listen_port
  $PY netem_proxy.py $4 127.0.0.1:$3 $1 $2 > "$OUT/proxy_$4.log" 2>&1 &
  PROXY=$!; sleep 0.5
}
ours() { # tag mbps delay args...
  local tag=$1 bw=$2 dl=$3; shift 3
  PORT=$((PORT + 2))
  proxy_start $bw $dl $PORT $((PORT + 1))
  taskset -c 47 $NB -role 1 -addr 127.0.0.1:$PORT -indexverify "$@" > "$OUT/$tag.recv.log" 2>&1 &
  local rp=$!; sleep 0.3
  taskset -c 46 $NB -role 0 -addr 127.0.0.1:$((PORT + 1)) "$@" > "$OUT/$tag.send.log" 2>&1
  local se=$?; wait $rp; local re=$?
  kill $PROXY 2>/dev/null; wait $PROXY 2>/dev/null
  echo "$tag sender_exit=$se receiver_exit=$re" >> "$OUT/summary.txt"
}
otf() { # tag mbps delay m n d otmode
  local tag=$1 bw=$2 dl=$3 m=$4 n=$5 d=$6 om=$7
  PORT=$((PORT + 2))
  proxy_start $bw $dl $PORT $((PORT + 1))
  OTFPSI_CLIENT_ADDR=127.0.0.1:$((PORT + 1)) /usr/bin/time -f "%e %U %S" -o "$OUT/$tag.time" taskset -c 46,47 $OT \
     -proto_distance batched -proto_threshold np -querier_size $m -responder_size $n -vector_size $d \
     -threshold 4 -prf aes -ot $om -localhost:1212 127.0.0.1:$PORT > "$OUT/$tag.log" 2>&1
  local e=$?
  kill $PROXY 2>/dev/null; wait $PROXY 2>/dev/null
  echo "$tag exit=$e" >> "$OUT/summary.txt"
}
for net in "wan 100 40" "lan 1000 0.5"; do
  set -- $net; NAME=$1 BW=$2 DL=$3
  for k in 1 2 3; do
    ours "${NAME}_o12_exact_r$k" $BW $DL -data "$OUT/syn12.bin" -d 128 -delta 4 -b 5 -s 1
    ours "${NAME}_o12_pow2_r$k" $BW $DL -data "$OUT/syn12.bin" -d 128 -delta 4 -b 5 -s 1 -pow2
    ours "${NAME}_o12_noU_r$k" $BW $DL -data "$OUT/syn12.bin" -d 128 -delta 4 -b 5 -s 1 -pow2 -noU
    ours "${NAME}_oc_exact_r$k" $BW $DL -data $CODE -d 64 -delta 3 -b 4 -s 1
    ours "${NAME}_oc_noU_r$k" $BW $DL -data $CODE -d 64 -delta 3 -b 4 -s 1 -pow2 -noU
    otf "${NAME}_ot12_silent_r$k" $BW $DL 4096 4096 128 silent
    otf "${NAME}_ot12_soft_r$k" $BW $DL 4096 4096 128 softspoken
    otf "${NAME}_otc_silent_r$k" $BW $DL 2973 2959 64 silent
  done
done
echo "done=$(date)" >> "$OUT/env.txt"
echo "$OUT"
