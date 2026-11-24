#!/usr/bin/env bash
# One output-matched otFPSI run: stage 1 = otFPSI native batched distances + secret-shared threshold
# (one process, both parties, protocol-scoped CPU printed), stage 2 = OR + OT delivery (or_deliver,
# two processes, receiver verifies against the plaintext). Both stages on cores 46,47.
# usage: run_matched.sh <data.bin> <d> <delta> <otmode> <outprefix> <port>
set -uo pipefail
DATA=$1 D=$2 DELTA=$3 OTM=$4 OUT=$5 PORT=$6
HERE=$(cd "$(dirname "$0")" && pwd)
OT=$HERE/../../baselines/others/xdup/01_otfpsi_cpu/build/otfpsi
ORD=$HERE/../../impl/build10/or_deliver
read M N < <(python3 -c "import struct,sys;m,n,d=struct.unpack('<QQQ',open(sys.argv[1],'rb').read(24));print(m,n)" "$DATA")
OTFPSI_MATCHED_DATA=$DATA OTFPSI_MATCHED_OUT=$OUT.shares /usr/bin/time -v -o $OUT.s1.time \
  taskset -c 46,47 $OT -proto_distance batched -proto_threshold np -querier_size $M -responder_size $N \
  -vector_size $D -threshold $DELTA -prf aes -ot $OTM -localhost:1212 127.0.0.1:$PORT > $OUT.s1.log 2>&1
echo "stage1 exit=$?" >> $OUT.status
/usr/bin/time -v -o $OUT.s2r.time taskset -c 47 $ORD -role 1 -addr 127.0.0.1:$((PORT+1)) -data $DATA -share $OUT.shares.R -d $D -delta $DELTA > $OUT.s2r.log 2>&1 &
RP=$!
sleep 0.3
/usr/bin/time -v -o $OUT.s2s.time taskset -c 46 $ORD -role 0 -addr 127.0.0.1:$((PORT+1)) -data $DATA -share $OUT.shares.S -d $D -delta $DELTA > $OUT.s2s.log 2>&1
echo "stage2 sender exit=$?" >> $OUT.status
wait $RP
echo "stage2 receiver exit=$?" >> $OUT.status
rm -f $OUT.shares.S $OUT.shares.R
