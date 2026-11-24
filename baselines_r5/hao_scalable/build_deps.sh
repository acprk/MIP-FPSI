#!/usr/bin/env bash
# Build volePSI (pinned commit from upstream build.sh) + BLAKE3 with g++-12, no sudo, no apt.
# Differences from upstream build.sh:
#  * no `apt install` (no root); compilers forced to gcc-12/g++-12
#  * no `--system`: dependencies (libOTe/cryptoTools/coproto/macoro) are fetched at their pinned
#    commits instead of picking up the unrelated versions installed in /usr/local
#  * Boost 1.86 is taken from /usr/local via a symlink-only prefix (boostprefix/) to avoid the
#    ~130 MB boost tarball download
#  * CMAKE_POLICY_VERSION_MINIMUM=3.5 because the host has CMake 4.x
#  * --par=4 (make -j4 at most)
set -euo pipefail
H=<ARTIFACT_ROOT>/baselines_r5/hao_scalable
R=$H/Scalable-FPSI-HEAD
export CC=/usr/bin/gcc-12 CXX=/usr/bin/g++-12
export CMAKE_POLICY_VERSION_MINIMUM=3.5
export CMAKE_BUILD_PARALLEL_LEVEL=4

cd $R/thirdparty/out/src/volepsi
python3 build.py --install=$R/thirdparty/out/install --par=4 \
  -DVOLE_PSI_ENABLE_BOOST=true -DVOLE_PSI_ENABLE_OPPRF=true -DVOLE_PSI_ENABLE_BITPOLYMUL=false \
  -DVOLE_PSI_SODIUM_MONTGOMERY=false -DFETCH_SPARSEHASH=true -DFETCH_LIBOTE=ON \
  -DCMAKE_PREFIX_PATH=$H/boostprefix \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-12 -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cp ./out/build/linux/volePSI/config.h $R/thirdparty/out/install/include/volePSI/

cd $R/thirdparty/out/src
if [ ! -d BLAKE3 ]; then git clone -q https://github.com/BLAKE3-team/BLAKE3.git; fi
cd BLAKE3
git checkout -q c7f0d216e6fc834b742456b39546c9835baa1277
cmake -S c -B c/build -DCMAKE_INSTALL_PREFIX=$R/thirdparty/out/install -DCMAKE_C_COMPILER=/usr/bin/gcc-12 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build c/build --target install -j4
echo DEPS_OK
