#include "Servers/Scene/SceneServer.h"
#include "Servers/App/ServerRpcSupport.h"
#include "Common/Runtime/Object/Object.h"

bool MSceneServer::LoadConfig(const MString& /*ConfigPath*/)
{
    return true;
}

bool MSceneServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    bRunning = true;
    MLogger::LogStartupBanner("SceneServer", Config.ListenPort, 0);
    MServerConnection::SetLocalInfo(4, EServerType::Scene, "SceneSkeleton");

    if (!SceneService)
    {
        SceneService = NewMObject<MSceneSessionServiceEndpoint>(this, "SceneService");
    }
    SceneService->Initialize(&PlayerScenes);
    return true;
}

void MSceneServer::Tick()
{
}

uint16 MSceneServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MSceneServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
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

void MSceneServer::ShutdownConnections()
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

void MSceneServer::OnRunStarted()
{
    LOG_INFO("Scene skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}

void MSceneServer::HandlePeerPacket(uint64 /*ConnectionId*/, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data)
{
    (void)MServerRpcSupport::DispatchServerCallPacket(SceneService, Connection, Data);
}
