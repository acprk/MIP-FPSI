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

// Added (e3): in-process transport. Loopback TCP is unavailable in the measurement sandbox, so the
// two party threads of the single otfpsi process talk over coproto::LocalAsyncSocket instead of
// AsioSocket on 127.0.0.1. Each protocol phase connects both parties once: the first caller creates
// the pair and takes its end, the second caller takes the other end.
#include "coproto/Socket/LocalAsyncSock.h"
#include <array>
#include <mutex>
#include <optional>
inline coproto::LocalAsyncSocket localConnect(bool server)
{
    static std::mutex mu;
    static std::optional<std::array<coproto::LocalAsyncSocket, 2>> pending;
    std::lock_guard<std::mutex> lk(mu);
    if (!pending)
    {
        pending.emplace(coproto::LocalAsyncSocket::makePair());
        return (*pending)[server ? 0 : 1];
    }
    coproto::LocalAsyncSocket s = (*pending)[server ? 0 : 1];
    pending.reset();
    return s;
}

using PRNG = osuCrypto::PRNG;
using BitVector = osuCrypto::BitVector;
using block = osuCrypto::block;
using Timer = osuCrypto::Timer;

template<typename T, size_t Alignment = osuCrypto::gDefaultAlignment> using AlignedUnVector = osuCrypto::AlignedUnVector<T, Alignment>;