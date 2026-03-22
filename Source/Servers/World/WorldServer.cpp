#include "Servers/World/WorldServer.h"
#include "Servers/App/ServerRpcSupport.h"

bool MWorldServer::LoadConfig(const MString& /*ConfigPath*/)
{
    return true;
}

bool MWorldServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    bRunning = true;
    MLogger::LogStartupBanner("WorldServer", Config.ListenPort, 0);
    MServerConnection::SetLocalInfo(3, EServerType::World, "WorldSkeleton");
    return true;
}

void MWorldServer::Tick()
{
}

uint16 MWorldServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MWorldServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    PeerConnections[ConnId] = Conn;
    LOG_INFO("World skeleton accepted connection %llu", static_cast<unsigned long long>(ConnId));
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

void MWorldServer::ShutdownConnections()
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

void MWorldServer::OnRunStarted()
{
    LOG_INFO("World skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}

MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> MWorldServer::PlayerEnterWorld(const FPlayerEnterWorldRequest& Request)
{
    return PlayerService.EnterWorld(OnlinePlayers, Request);
}

MFuture<TResult<FPlayerFindResponse, FAppError>> MWorldServer::PlayerFind(const FPlayerFindRequest& Request)
{
    return PlayerService.FindPlayer(OnlinePlayers, Request);
}

MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> MWorldServer::PlayerUpdateRoute(const FPlayerUpdateRouteRequest& Request)
{
    return PlayerService.UpdateRoute(OnlinePlayers, Request);
}

MFuture<TResult<FPlayerLogoutResponse, FAppError>> MWorldServer::PlayerLogout(const FPlayerLogoutRequest& Request)
{
    return PlayerService.Logout(OnlinePlayers, Request);
}

MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> MWorldServer::PlayerSwitchScene(const FPlayerSwitchSceneRequest& Request)
{
    return PlayerService.SwitchScene(OnlinePlayers, Request);
}

void MWorldServer::HandlePeerPacket(uint64 /*ConnectionId*/, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data)
{
    (void)MServerRpcSupport::DispatchServerCallPacket(this, Connection, Data);
}
