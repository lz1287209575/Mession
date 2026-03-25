#include "Servers/Mgo/MgoServer.h"
#include "Servers/App/ServerRpcSupport.h"
#include "Common/Runtime/Object/Object.h"

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

    if (!PlayerStateService)
    {
        PlayerStateService = NewMObject<MMgoPlayerStateServiceEndpoint>(this, "PlayerStateService");
    }
    PlayerStateService->Initialize(&PlayerPersistenceRecords);
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
    ClearRpcTransports();
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
    if (!PlayerStateService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FMgoLoadPlayerResponse>(
            "mgo_service_missing",
            "LoadPlayer");
    }

    return PlayerStateService->LoadPlayer(Request);
}

MFuture<TResult<FMgoSavePlayerResponse, FAppError>> MMgoServer::SavePlayer(const FMgoSavePlayerRequest& Request)
{
    if (!PlayerStateService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FMgoSavePlayerResponse>(
            "mgo_service_missing",
            "SavePlayer");
    }

    return PlayerStateService->SavePlayer(Request);
}

void MMgoServer::HandlePeerPacket(uint64 /*ConnectionId*/, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data)
{
    (void)MServerRpcSupport::DispatchServerCallPacket(this, Connection, Data);
}
