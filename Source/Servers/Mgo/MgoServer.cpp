#include "Servers/Mgo/MgoServer.h"
#include "Servers/App/ServerRpcSupport.h"

bool MMgoServer::LoadConfig(const MString& /*ConfigPath*/)
{
    return true;
}

bool MMgoServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    bRunning = true;
    MLogger::LogStartupBanner("MgoServer", Config.ListenPort, 0);
    MServerConnection::SetLocalInfo(6, EServerType::Mgo, "MgoSkeleton");
    return true;
}

void MMgoServer::Tick()
{
}

uint16 MMgoServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MMgoServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
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

void MMgoServer::ShutdownConnections()
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

void MMgoServer::OnRunStarted()
{
    LOG_INFO("Mgo skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}

MFuture<TResult<FMgoLoadPlayerResponse, FAppError>> MMgoServer::LoadPlayer(const FMgoLoadPlayerRequest& Request)
{
    return PlayerStateService.LoadPlayer(PlayerPayloads, Request);
}

MFuture<TResult<FMgoSavePlayerResponse, FAppError>> MMgoServer::SavePlayer(const FMgoSavePlayerRequest& Request)
{
    return PlayerStateService.SavePlayer(PlayerPayloads, Request);
}

void MMgoServer::HandlePeerPacket(uint64 /*ConnectionId*/, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data)
{
    (void)MServerRpcSupport::DispatchServerCallPacket(this, Connection, Data);
}
