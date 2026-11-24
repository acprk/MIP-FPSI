#!/usr/bin/env bash
# Build otfpsi_inproc: the 01_otfpsi_cpu sources (read in place, not modified) compiled with the
# same flags/libraries as baselines/others/xdup/01_otfpsi_cpu/build, except that shim/ is first on
# the include path, replacing the TCP AsioSocket with coproto's in-memory LocalAsyncSocket.
set -euo pipefail
D=<ARTIFACT_ROOT>/experiments_r6/otfpsi_inproc
S=<ARTIFACT_ROOT>/baselines/others/xdup/01_otfpsi_cpu
P=<ARTIFACT_ROOT>/baselines/others/prefix
DEF="-DBOOST_SYSTEM_NO_LIB -DBOOST_THREAD_NO_LIB -DENABLE_SHA -DUSE_INTEL_AESNI"
FL="-include optional -include cstdint -O3 -march=native -flto -Wall -Wextra -std=gnu++20 -maes -msse2 -msse3 -mssse3 -msse4.1 -mpclmul -mavx2 -Wno-error=switch"
INC="-I$D/shim -isystem $P/include"
OBJS=""
for f in distance_computation/batched_distance_computation distance_computation/individual_distance_computation \
         distance_computation/shared_batched_distance_computation main threshold_comparison/np_threshold_comparison \
         util/prf/prf_aes_ctr util/prf/prf_blake3 util/prf/prf_sha256 util/prf/sha256; do
  o=$D/obj/$(basename $f).o
  extra=""; [ "$f" = util/prf/sha256 ] && extra="-msha -msse4.1"
  /usr/bin/c++ $DEF $INC $FL $extra -o $o -c $S/$f.cpp
  OBJS="$OBJS $o"
done
/usr/bin/c++ $FL -L$P/lib -pthread -fPIC -no-pie $OBJS -o $D/otfpsi_inproc $P/lib/liblibOTe.a -lblake3 $P/lib/libSimplestOT.a $P/lib/libcryptoTools.a $P/lib/libsodium.a $P/lib/libcoproto.a $P/lib/libboost_system.a $P/lib/libboost_thread.a $P/lib/libmacoro.a $P/lib/libKyberOT.a
sha256sum $D/otfpsi_inproc $S/build/otfpsi
