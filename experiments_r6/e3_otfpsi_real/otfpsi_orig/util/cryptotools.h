#pragma once

// Includes and defines the most important types from the osuCrypto cryptoTools library

#include "cryptoTools/Crypto/PRNG.h"
#include "cryptoTools/Common/BitVector.h"
#include "cryptoTools/Common/Timer.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Common/Aligned.h"
#include <sys/resource.h>

// Added for the FPSI comparison: process CPU seconds (user + system, all threads).
inline double processCpuSeconds()
{
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return u.ru_utime.tv_sec + u.ru_stime.tv_sec + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
}

using PRNG = osuCrypto::PRNG;
using BitVector = osuCrypto::BitVector;
using block = osuCrypto::block;
using Timer = osuCrypto::Timer;

template<typename T, size_t Alignment = osuCrypto::gDefaultAlignment> using AlignedUnVector = osuCrypto::AlignedUnVector<T, Alignment>;