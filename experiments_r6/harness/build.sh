#!/usr/bin/env bash
# Build fpsi_bench_kc: impl/src/main.cpp + one line parsing -keycheck (cfg.keyCheck), linked
# against the byte-identical libmipfpsi.a of audit_r5/build (= impl/build11). Same flags as CMake.
# Also rebuilds the unmodified main.cpp (fpsi_bench_ref) to check the toolchain reproduces main.o.
set -euo pipefail
H=<ARTIFACT_ROOT>/experiments_r6/harness
A=<ARTIFACT_ROOT>/audit_r5/build
V=<VOLEPSI_ROOT>/out/install/linux
FL="-Wall -Wfatal-errors -march=native -mavx -maes -msse2 -msse3 -mssse3 -msse4.1 -mpclmul -mavx2 -Wno-error=switch -pthread -O3 -DNDEBUG -std=gnu++20"
DEF="-DBOOST_SYSTEM_NO_LIB -DBOOST_THREAD_NO_LIB"
INC="-I<ARTIFACT_ROOT>/impl/src -I<ARTIFACT_ROOT>/impl/../third_party/volepsi/out/build -I<ARTIFACT_ROOT>/impl/../third_party/volepsi -isystem $V/include"
LIBS="$A/libmipfpsi.a <ARTIFACT_ROOT>/impl/../third_party/volepsi/out/build/volePSI/libvolePSI.a $V/lib/liblibOTe.a $V/lib/libcryptoTools.a $V/lib/libsodium.a $V/lib/libcoproto.a $V/lib/libboost_system.a $V/lib/libboost_thread.a $V/lib/libmacoro.a $V/lib/libbitpolymul.a"
/usr/bin/c++ $DEF $INC $FL -o $H/main_kc.o -c $H/main_kc.cpp
/usr/bin/c++ $FL $H/main_kc.o -o $H/fpsi_bench_kc $LIBS
/usr/bin/c++ $DEF $INC $FL -o $H/main_ref.o -c <ARTIFACT_ROOT>/impl/src/main.cpp
/usr/bin/c++ $FL $H/main_ref.o -o $H/fpsi_bench_ref $LIBS
sha256sum $H/main_ref.o $A/CMakeFiles/fpsi_bench.dir/src/main.cpp.o $H/fpsi_bench_ref $A/fpsi_bench $H/fpsi_bench_kc $A/libmipfpsi.a
