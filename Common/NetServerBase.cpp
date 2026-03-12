#include "NetServerBase.h"
#include "Common/Logger.h"

void MNetServerBase::Run()
{
    if (!bRunning)
    {
        LOG_ERROR("Server not initialized (bRunning false)");
        return;
    }

    const uint16 Port = GetListenPort();
    ListenerId = EventLoop.RegisterListener(Port, [this](uint64 ConnId, TSharedPtr<INetConnection> Conn)
    {
        OnAccept(ConnId, Conn);
    });

    if (ListenerId == 0)
    {
        LOG_ERROR("Failed to register listener on port %d", Port);
        return;
    }

    OnRunStarted();

    while (bRunning)
    {
        EventLoop.RunOnce(16);
        TickBackends();
    }

    EventLoop.UnregisterListener(ListenerId);
    ListenerId = 0;
}

void MNetServerBase::RequestShutdown()
{
    bRunning = false;
    EventLoop.Stop();
}

void MNetServerBase::Shutdown()
{
    if (bShutdownDone)
    {
        return;
    }
    bShutdownDone = true;
    bRunning = false;
    ShutdownConnections();
    if (ListenerId != 0)
    {
        EventLoop.UnregisterListener(ListenerId);
        ListenerId = 0;
    }
}
