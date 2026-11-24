#pragma once
// experiments_r6 transport shim for the otFPSI artifact (01_otfpsi_cpu sources, unmodified).
// Loopback TCP is blocked in this sandbox, so this header shadows coproto/Socket/AsioSocket.h
// (placed first on the include path). AsioSocket becomes coproto's in-memory LocalAsyncSocket and
// asioConnect(ip, server) hands out the two ends of a fresh LocalAsyncSocket pair: the first of
// the two connecting threads creates the pair, the second takes the other end. The protocol code,
// OT choice, communication accounting (bytesSent/bytesReceived) and threading are unchanged.
#include "coproto/config.h"
#include "coproto/Socket/Socket.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "coproto/Common/Optional.h"
#include "coproto/Common/macoro.h"
#include <array>
#include <mutex>
#include <optional>
#include <string>

namespace coproto
{
    using AsioSocket = LocalAsyncSocket;

    namespace shim_detail
    {
        inline std::mutex& mtx() { static std::mutex m; return m; }
        inline std::optional<std::array<LocalAsyncSocket, 2>>& pending()
        {
            static std::optional<std::array<LocalAsyncSocket, 2>> p;
            return p;
        }
        inline bool& pendingServer() { static bool s = false; return s; }
    }

    inline LocalAsyncSocket asioConnect(std::string, bool server)
    {
        std::lock_guard<std::mutex> lk(shim_detail::mtx());
        auto& p = shim_detail::pending();
        if (!p)
        {
            p.emplace(LocalAsyncSocket::makePair());
            shim_detail::pendingServer() = server;
            return std::move((*p)[server ? 0 : 1]);
        }
        if (shim_detail::pendingServer() == server)
            throw std::runtime_error("shim: two connects with the same role");
        LocalAsyncSocket s = std::move((*p)[server ? 0 : 1]);
        p.reset();
        return s;
    }
}
