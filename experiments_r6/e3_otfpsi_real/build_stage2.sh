#!/usr/bin/env bash
# Build or_deliver_local with exactly the compile/link flags of impl/build10/or_deliver
# (impl/build10/CMakeFiles/or_deliver.dir/{flags.make,link.txt}), linking the same libmipfpsi.a.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
IMPL=$HERE/../../impl
INST=<VOLEPSI_ROOT>/out/install/linux
FLAGS="-Wall -Wfatal-errors -march=native -mavx -maes -msse2 -msse3 -mssse3 -msse4.1 -mpclmul -mavx2 -Wno-error=switch -pthread -O3 -DNDEBUG"
mkdir -p "$HERE/build_stage2"
/usr/bin/c++ -DBOOST_SYSTEM_NO_LIB -DBOOST_THREAD_NO_LIB -I"$IMPL/src" -I"$IMPL/../third_party/volepsi/out/build" \
    -I"$IMPL/../third_party/volepsi" -isystem "$INST/include" $FLAGS -std=gnu++20 \
    -c "$HERE/or_deliver_local.cpp" -o "$HERE/build_stage2/or_deliver_local.o"
/usr/bin/c++ $FLAGS "$HERE/build_stage2/or_deliver_local.o" -o "$HERE/build_stage2/or_deliver_local" \
    "$IMPL/build10/libmipfpsi.a" "$IMPL/../third_party/volepsi/out/build/volePSI/libvolePSI.a" \
    "$INST/lib/liblibOTe.a" "$INST/lib/libcryptoTools.a" "$INST/lib/libsodium.a" "$INST/lib/libcoproto.a" \
    "$INST/lib/libboost_system.a" "$INST/lib/libboost_thread.a" "$INST/lib/libmacoro.a" "$INST/lib/libbitpolymul.a"
sha256sum "$HERE/build_stage2/or_deliver_local" "$IMPL/build10/libmipfpsi.a"
