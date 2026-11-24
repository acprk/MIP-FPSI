cmake_minimum_required(VERSION 3.15)
project(BallExpBaseline CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Unmodified upstream volePSI (commit ec76012) built in third_party/volepsi/out/build; deps from an
# install prefix of unmodified upstream libOTe/cryptoTools/coproto/macoro builds.
set(VOLEPSI_SRC "${CMAKE_SOURCE_DIR}/../../third_party/volepsi" CACHE PATH "volePSI source root")
set(VOLEPSI_BUILD "${VOLEPSI_SRC}/out/build" CACHE PATH "volePSI build dir")
set(VOLEPSI_INSTALL "<VOLEPSI_ROOT>/out/install/linux" CACHE PATH "dependency install prefix")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wfatal-errors -march=native -maes -mpclmul -mavx2 -pthread")
add_compile_definitions(BOOST_SYSTEM_NO_LIB BOOST_THREAD_NO_LIB)

include_directories(${VOLEPSI_BUILD} ${VOLEPSI_SRC})
include_directories(SYSTEM ${VOLEPSI_INSTALL}/include)

add_executable(ballexp_bench src/ballexp_bench.cpp)
target_link_libraries(ballexp_bench
  ${VOLEPSI_BUILD}/volePSI/libvolePSI.a
  ${VOLEPSI_INSTALL}/lib/liblibOTe.a
  ${VOLEPSI_INSTALL}/lib/libcryptoTools.a
  ${VOLEPSI_INSTALL}/lib/libsodium.a
  ${VOLEPSI_INSTALL}/lib/libcoproto.a
  ${VOLEPSI_INSTALL}/lib/libboost_system.a
  ${VOLEPSI_INSTALL}/lib/libboost_thread.a
  ${VOLEPSI_INSTALL}/lib/libmacoro.a
  ${VOLEPSI_INSTALL}/lib/libbitpolymul.a
)
