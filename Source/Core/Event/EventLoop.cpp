#include "EventLoop.h"
#include <algorithm>

uint64 MNetEventLoop::RegisterListener(uint16 Port, TAcceptCallback OnAccept)
{
    if (!MSocket::EnsureInit())
    {
        return 0;
    }
    TSocketFd Fd = MSocket::CreateListenSocket(Port, 128);
    if (Fd == INVALID_SOCKET_FD)
    {
        return 0;
    }
    const uint64 Id = NextListenerId++;
    SListener L;
    L.Id = Id;
    L.Handle.Reset(Fd);
    L.Port = Port;
    L.OnAccept = std::move(OnAccept);
    Listeners.push_back(std::move(L));
    return Id;
}

void MNetEventLoop::UnregisterListener(uint64 ListenerId)
{
    auto It = std::remove_if(Listeners.begin(), Listeners.end(),
        [ListenerId](const SListener& L) { return L.Id == ListenerId; });
    Listeners.erase(It, Listeners.end());
}

void MNetEventLoop::RegisterConnection(uint64 ConnectionId, TSharedPtr<INetConnection> Connection,
                                      TReadCallback OnRead, TCloseCallback OnClose)
{
    if (!Connection || !Connection->IsConnected())
    {
        return;
    }
    SConnection C;
    C.Id = ConnectionId;
    C.Conn = std::move(Connection);
    C.OnRead = std::move(OnRead);
    C.OnClose = std::move(OnClose);
    Connections[ConnectionId] = std::move(C);
}

void MNetEventLoop::UnregisterConnection(uint64 ConnectionId)
{
    Connections.erase(ConnectionId);
}

void MNetEventLoop::RunOnce(int TimeoutMs)
{
    TVector<pollfd> PollFds;
    TVector<TPair<bool, uint64>> PollMeta;

    for (const SListener& L : Listeners)
    {
        if (!L.Handle.IsValid())
        {
            continue;
        }
        pollfd P = {};
        P.fd = L.Handle.Get();
        P.events = POLLIN;
        P.revents = 0;
        PollFds.push_back(P);
        PollMeta.push_back(TPair<bool, uint64>(true, L.Id));
    }

    for (auto& [Id, C] : Connections)
    {
        (void)Id;
        if (!C.Conn || !C.Conn->IsConnected())
        {
            continue;
        }
        C.Conn->FlushSendBuffer();
        pollfd P = {};
        P.fd = C.Conn->GetSocketFd();
        P.events = POLLIN;
        P.revents = 0;
        PollFds.push_back(P);
        PollMeta.push_back(TPair<bool, uint64>(false, C.Id));
    }

    if (PollFds.empty())
    {
        return;
    }

    const int N = poll(PollFds.data(), PollFds.size(), TimeoutMs);
    if (N < 0 || N == 0)
    {
        return;
    }

    TVector<uint64> ToClose;

    for (size_t i = 0; i < PollFds.size(); ++i)
    {
        if (PollFds[i].revents == 0)
        {
            continue;
        }
        const bool bListener = PollMeta[i].first;
        const uint64 Id = PollMeta[i].second;

        if (bListener)
        {
            for (SListener& L : Listeners)
            {
                if (L.Id != Id || !L.Handle.IsValid())
                {
                    continue;
                }
                SAcceptedSocket Accepted = MSocket::AcceptConnection(L.Handle.Get());
                if (!Accepted.IsValid())
                {
                    break;
                }
                const uint64 ConnId = MUniqueIdGenerator::Generate();
                TSharedPtr<INetConnection> Conn = MakeShared<MTcpConnection>(
                    std::move(Accepted.Socket), Accepted.RemoteAddress, Accepted.RemotePort);
                if (L.OnAccept)
                {
                    L.OnAccept(ConnId, Conn);
                }
                break;
            }
            continue;
        }

        auto It = Connections.find(Id);
        if (It == Connections.end())
        {
            continue;
        }
        SConnection& C = It->second;
        if (!C.Conn || !C.Conn->IsConnected())
        {
            ToClose.push_back(Id);
            continue;
        }

        const short Rev = PollFds[i].revents;
        if ((Rev & (POLLERR | POLLHUP)) != 0)
        {
            if (C.OnClose)
            {
                C.OnClose(Id);
            }
            ToClose.push_back(Id);
            continue;
        }

        if ((Rev & POLLIN) != 0)
        {
            TArray Packet;
            while (C.Conn->ReceivePacket(Packet))
            {
                if (C.OnRead)
                {
                    C.OnRead(Id, Packet);
                }
                Packet.clear();
            }

            if (!C.Conn->IsConnected())
            {
                if (C.OnClose)
                {
                    C.OnClose(Id);
                }
                ToClose.push_back(Id);
            }
        }
    }

    for (uint64 Id : ToClose)
    {
        Connections.erase(Id);
    }
}

void MNetEventLoop::Run()
{
    bRunning = true;
    while (bRunning)
    {
        RunOnce(100);
    }
}
