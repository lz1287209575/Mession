#pragma once

#include "Common/IO/Socket/Socket.h"
#include <cstddef>

// Cross-platform poll() abstraction: POSIX poll on Unix, WSAPoll on Windows.
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64)
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    typedef WSAPOLLFD pollfd;
    #ifndef POLLIN
        #define POLLIN   (1 << 0)
    #endif
    #ifndef POLLOUT
        #define POLLOUT  (1 << 2)
    #endif
    #ifndef POLLERR
        #define POLLERR  (1 << 3)
    #endif
    #ifndef POLLHUP
        #define POLLHUP  (1 << 4)
    #endif

    inline int poll(pollfd* Fds, size_t Nfds, int TimeoutMs)
    {
        return WSAPoll(Fds, (ULONG)Nfds, TimeoutMs);
    }
#else
    #include <poll.h>
#endif

struct SSocketPollItem
{
    uint64 ConnectionId = 0;
    pollfd PollFd{};
};

struct SSocketPollResult
{
    uint64 ConnectionId = 0;
    short Revents = 0;
};

class MSocketPoller
{
public:
    template<typename TConnectionMap, typename TGetConnection>
    static TVector<SSocketPollItem> BuildReadableItems(TConnectionMap& Connections, TGetConnection GetConnection)
    {
        TVector<SSocketPollItem> PollItems;
        PollItems.reserve(Connections.size());

        for (auto& [ConnectionId, Entry] : Connections)
        {
            INetConnection* Connection = GetConnection(Entry);
            if (!Connection || !Connection->IsConnected())
            {
                continue;
            }

            Connection->FlushSendBuffer();

            SSocketPollItem Item;
            Item.ConnectionId = ConnectionId;
            Item.PollFd.fd = Connection->GetSocketFd();
            Item.PollFd.events = POLLIN;
            Item.PollFd.revents = 0;
            PollItems.push_back(Item);
        }

        return PollItems;
    }

    static int32 PollReadable(const TVector<SSocketPollItem>& PollItems, TVector<SSocketPollResult>& OutResults, int TimeoutMs)
    {
        OutResults.clear();
        if (PollItems.empty())
        {
            return 0;
        }

        TVector<pollfd> PollFds;
        PollFds.reserve(PollItems.size());
        for (const SSocketPollItem& Item : PollItems)
        {
            PollFds.push_back(Item.PollFd);
        }

        const int32 Ret = poll(PollFds.data(), PollFds.size(), TimeoutMs);
        if (Ret <= 0)
        {
            return Ret;
        }

        OutResults.reserve(PollItems.size());
        for (size_t Index = 0; Index < PollItems.size(); ++Index)
        {
            if (PollFds[Index].revents == 0)
            {
                continue;
            }

            SSocketPollResult Result;
            Result.ConnectionId = PollItems[Index].ConnectionId;
            Result.Revents = PollFds[Index].revents;
            OutResults.push_back(Result);
        }

        return Ret;
    }

    static bool IsReadable(const SSocketPollResult& PollResult)
    {
        return (PollResult.Revents & POLLIN) != 0;
    }

    static bool HasError(const SSocketPollResult& PollResult)
    {
        return (PollResult.Revents & (POLLERR | POLLHUP)) != 0;
    }
};
