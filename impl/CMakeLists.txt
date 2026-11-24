cmake_minimum_required(VERSION 3.15)
project(MipFpsi CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Dependencies: unmodified upstream volePSI (Visa-Research, MIT) at commit
# ec76012ed516e25d3f460af9b8680e1140a5d491, plus the libraries it pulls in (libOTe, cryptoTools,
# coproto, macoro, libsodium, boost, bitpolymul). deps/fetch_volepsi.sh clones and builds them in the
# layout below. Every path is a cache variable, so an existing build can be used instead, e.g.
#   cmake -S impl -B build -DVOLEPSI_ROOT=/path/to/volepsi \
#         -DVOLEPSI_BUILD=/path/to/volepsi/out/build/linux \
#         -DVOLEPSI_DEPS_PREFIX=/path/to/volepsi/out/install/linux
# The names VOLEPSI_SRC / VOLEPSI_INSTALL of the original build directories (impl/buildN) are
# accepted as aliases of VOLEPSI_ROOT / VOLEPSI_DEPS_PREFIX.
if(DEFINED VOLEPSI_SRC AND NOT DEFINED VOLEPSI_ROOT)
  set(VOLEPSI_ROOT "${VOLEPSI_SRC}" CACHE PATH "volePSI source root")
endif()
if(DEFINED VOLEPSI_INSTALL AND NOT DEFINED VOLEPSI_DEPS_PREFIX)
  set(VOLEPSI_DEPS_PREFIX "${VOLEPSI_INSTALL}" CACHE PATH "dependency install prefix")
endif()
set(VOLEPSI_ROOT "${CMAKE_SOURCE_DIR}/../deps/volepsi" CACHE PATH
    "volePSI source root (git checkout of Visa-Research/volepsi @ ec76012)")
set(VOLEPSI_BUILD "${VOLEPSI_ROOT}/out/build/linux" CACHE PATH
    "volePSI build directory (contains volePSI/libvolePSI.a and the generated volePSI/config.h)")
set(VOLEPSI_DEPS_PREFIX "${VOLEPSI_ROOT}/out/install/linux" CACHE PATH
    "install prefix of libOTe, cryptoTools, coproto, macoro, sodium, boost, bitpolymul")

foreach(f "${VOLEPSI_ROOT}/volePSI/RsCpsi.h"
          "${VOLEPSI_BUILD}/volePSI/libvolePSI.a"
          "${VOLEPSI_DEPS_PREFIX}/lib/liblibOTe.a")
  if(NOT EXISTS "${f}")
    message(FATAL_ERROR "missing ${f}\n"
      "Build the pinned dependencies first (deps/fetch_volepsi.sh), or point VOLEPSI_ROOT, "
      "VOLEPSI_BUILD and VOLEPSI_DEPS_PREFIX at an existing build. See README.md, section Build.")
  endif()
endforeach()

# Flags used for every timed build (GCC 11.4.0, x86-64 with AVX2/AES-NI/PCLMUL).
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wfatal-errors -march=native -mavx -maes -msse2 -msse3 -mssse3 -msse4.1 -mpclmul -mavx2 -Wno-error=switch -pthread")
add_compile_definitions(BOOST_SYSTEM_NO_LIB BOOST_THREAD_NO_LIB)

include_directories(
  ${CMAKE_SOURCE_DIR}/src
  ${VOLEPSI_BUILD}
  ${VOLEPSI_ROOT}
)
include_directories(SYSTEM ${VOLEPSI_DEPS_PREFIX}/include)

set(DEP_LIBS
  ${VOLEPSI_BUILD}/volePSI/libvolePSI.a
  ${VOLEPSI_DEPS_PREFIX}/lib/liblibOTe.a
  ${VOLEPSI_DEPS_PREFIX}/lib/libcryptoTools.a
  ${VOLEPSI_DEPS_PREFIX}/lib/libsodium.a
  ${VOLEPSI_DEPS_PREFIX}/lib/libcoproto.a
  ${VOLEPSI_DEPS_PREFIX}/lib/libboost_system.a
  ${VOLEPSI_DEPS_PREFIX}/lib/libboost_thread.a
  ${VOLEPSI_DEPS_PREFIX}/lib/libmacoro.a
  ${VOLEPSI_DEPS_PREFIX}/lib/libbitpolymul.a
)

add_library(mipfpsi STATIC
  src/circuits.cpp
  src/mip.cpp
  src/HamCpsi.cpp
  src/osn.cpp
  src/oep.cpp
  src/ot_threshold.cpp
  src/fpsi.cpp
)
target_link_libraries(mipfpsi ${DEP_LIBS})

add_executable(fpsi_bench src/main.cpp)
target_link_libraries(fpsi_bench mipfpsi ${DEP_LIBS})

add_executable(gmw_bench src/gmw_bench.cpp)
target_link_libraries(gmw_bench mipfpsi ${DEP_LIBS})

add_executable(unit_tests src/unit_tests.cpp)
target_link_libraries(unit_tests mipfpsi ${DEP_LIBS})

add_executable(choose_cover src/choose_cover.cpp)
target_link_libraries(choose_cover mipfpsi ${DEP_LIBS})

add_executable(net_bench src/net_bench.cpp)
target_link_libraries(net_bench mipfpsi ${DEP_LIBS})

add_executable(cover_dump src/cover_dump.cpp)
target_link_libraries(cover_dump mipfpsi ${DEP_LIBS})

add_executable(or_deliver src/or_deliver.cpp)
target_link_libraries(or_deliver mipfpsi ${DEP_LIBS})
