cmake_minimum_required(VERSION 3.15)
project(E6CandStats CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(IMPL <ARTIFACT_ROOT>/impl)
set(VOLEPSI_SRC "${IMPL}/../third_party/volepsi")
set(VOLEPSI_BUILD "${VOLEPSI_SRC}/out/build")
set(VOLEPSI_INSTALL "<VOLEPSI_ROOT>/out/install/linux")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -maes -msse4.1 -mpclmul -mavx2 -pthread")
add_compile_definitions(BOOST_SYSTEM_NO_LIB BOOST_THREAD_NO_LIB)
include_directories(${IMPL}/src ${VOLEPSI_BUILD} ${VOLEPSI_SRC})
include_directories(SYSTEM ${VOLEPSI_INSTALL}/include)
add_executable(cand_stats cand_stats.cpp)
# links the unchanged protocol library of build12 (MipParams::make, uniqueRows, hamming)
target_link_libraries(cand_stats ${IMPL}/build12/libmipfpsi.a
  ${VOLEPSI_BUILD}/volePSI/libvolePSI.a
  ${VOLEPSI_INSTALL}/lib/liblibOTe.a ${VOLEPSI_INSTALL}/lib/libcryptoTools.a
  ${VOLEPSI_INSTALL}/lib/libsodium.a ${VOLEPSI_INSTALL}/lib/libcoproto.a
  ${VOLEPSI_INSTALL}/lib/libboost_system.a ${VOLEPSI_INSTALL}/lib/libboost_thread.a
  ${VOLEPSI_INSTALL}/lib/libmacoro.a ${VOLEPSI_INSTALL}/lib/libbitpolymul.a)
