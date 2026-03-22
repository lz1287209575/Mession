#include "Servers/Router/RouterServer.h"
#include "Servers/App/ServerRpcSupport.h"

bool MRouterServer::LoadConfig(const MString& /*ConfigPath*/)
{
    return true;
}

bool MRouterServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    bRunning = true;
    MLogger::LogStartupBanner("RouterServer", Config.ListenPort, 0);
    MServerConnection::SetLocalInfo(5, EServerType::Router, "RouterSkeleton");
    return true;
}

void MRouterServer::Tick()
{
}

uint16 MRouterServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MRouterServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    PeerConnections[ConnId] = Conn;
    EventLoop.RegisterConnection(
        ConnId,
        Conn,
        [this, Conn](uint64 ConnectionId, const TByteArray& Payload)
        {
            HandlePeerPacket(ConnectionId, Conn, Payload);
        },
        [this](uint64 ConnectionId)
        {
            PeerConnections.erase(ConnectionId);
        });
}

void MRouterServer::ShutdownConnections()
{
    for (auto& [ConnId, Conn] : PeerConnections)
    {
        (void)ConnId;
        if (Conn)
        {
            Conn->Close();
        }
    }
    PeerConnections.clear();
}

void MRouterServer::OnRunStarted()
{
    LOG_INFO("Router skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}

MFuture<TResult<FRouterResolvePlayerRouteResponse, FAppError>> MRouterServer::ResolvePlayerRoute(const FRouterResolvePlayerRouteRequest& Request)
{
    return RegistryService.ResolvePlayerRoute(Routes, Request);
}

MFuture<TResult<FRouterUpsertPlayerRouteResponse, FAppError>> MRouterServer::UpsertPlayerRoute(const FRouterUpsertPlayerRouteRequest& Request)
{
    return RegistryService.UpsertPlayerRoute(Routes, Request);
}

void MRouterServer::HandlePeerPacket(uint64 /*ConnectionId*/, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data)
{
    (void)MServerRpcSupport::DispatchServerCallPacket(this, Connection, Data);
}
