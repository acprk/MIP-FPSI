#!/usr/bin/env bash
# Fetch and build the pinned volePSI dependency tree used by impl/CMakeLists.txt.
#
#   deps/fetch_volepsi.sh [DEST]          (default DEST = deps/volepsi next to this script)
#
# Result (the defaults of impl/CMakeLists.txt):
#   DEST/                         git checkout of https://github.com/Visa-Research/volepsi.git
#                                 at ec76012ed516e25d3f460af9b8680e1140a5d491 (unmodified)
#   DEST/out/build/linux/         volePSI build (volePSI/libvolePSI.a, volePSI/config.h)
#   DEST/out/install/linux/       libOTe, cryptoTools, coproto, macoro, libsodium, boost, bitpolymul,
#                                 fetched by volePSI's own dependency manager (FETCH_AUTO=ON);
#                                 libOTe is pinned by volePSI to d55867114c78272be7142bd67ebdcb346fec8621
#                                 (thirdparty/getLibOTe.cmake), which in turn pins its own submodules.
#
# Options: the VOLE_PSI_* values are the ones recorded in the CMakeCache.txt of the volePSI build that
# was linked into every timed binary (deps/records/volepsi_CMakeCache.txt): Release, SSE on, boost on,
# sodium on with the Montgomery variant, bitpolymul on, GMW/CPSI/OPPRF on, OpenSSL/relic/PIC/ASAN off.
# Toolchain of the timed builds: GCC 11.4.0 (/usr/bin/c++), CMake 4.4.3, Ubuntu 22.04, x86-64.
#
# Caveat (see README.md, "Known limitations"): the timed builds used dependency libraries from a
# pre-existing install prefix on the author's machine, configured with FETCH_AUTO=OFF; their exact
# commits were not recorded with the experiments. This script rebuilds them from the versions that
# volePSI @ ec76012 pins. deps/record_dep_versions.sh prints the commits of an existing dependency tree.
set -euo pipefail

VOLEPSI_URL=https://github.com/Visa-Research/volepsi.git
VOLEPSI_COMMIT=ec76012ed516e25d3f460af9b8680e1140a5d491
HERE=$(cd "$(dirname "$0")" && pwd)
DEST=${1:-$HERE/volepsi}
PAR=${PAR:-$(nproc)}

if [ ! -d "$DEST/.git" ]; then
  git clone "$VOLEPSI_URL" "$DEST"
fi
git -C "$DEST" fetch --quiet origin || true
git -C "$DEST" checkout --quiet "$VOLEPSI_COMMIT"
test "$(git -C "$DEST" rev-parse HEAD)" = "$VOLEPSI_COMMIT"
test -z "$(git -C "$DEST" status --porcelain)" || { echo "volePSI checkout is modified" >&2; exit 1; }

cd "$DEST"
export CC=${CC:-gcc} CXX=${CXX:-g++}
python3 build.py --par="$PAR" \
  -DVOLE_PSI_ENABLE_SSE=ON \
  -DVOLE_PSI_ENABLE_BOOST=ON \
  -DVOLE_PSI_ENABLE_SODIUM=ON \
  -DVOLE_PSI_SODIUM_MONTGOMERY=ON \
  -DVOLE_PSI_ENABLE_BITPOLYMUL=ON \
  -DVOLE_PSI_ENABLE_GMW=ON \
  -DVOLE_PSI_ENABLE_CPSI=ON \
  -DVOLE_PSI_ENABLE_OPPRF=ON \
  -DVOLE_PSI_ENABLE_OPENSSL=OFF \
  -DVOLE_PSI_ENABLE_RELIC=OFF \
  -DVOLE_PSI_ENABLE_PIC=OFF \
  -DVOLE_PSI_ENABLE_ASAN=OFF

test -f out/build/linux/volePSI/libvolePSI.a
test -f out/install/linux/lib/liblibOTe.a
echo "volePSI $VOLEPSI_COMMIT built in $DEST/out/build/linux; dependencies in $DEST/out/install/linux"
"$HERE/record_dep_versions.sh" "$DEST/out" || true
