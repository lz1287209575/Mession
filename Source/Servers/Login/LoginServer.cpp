#include "Servers/Login/LoginServer.h"
#include "Servers/App/ServerRpcSupport.h"
#include "Common/Runtime/Object/Object.h"

bool MLoginServer::LoadConfig(const MString& /*ConfigPath*/)
{
    return true;
}

bool MLoginServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    bRunning = true;
    MLogger::LogStartupBanner("LoginServer", Config.ListenPort, 0);
    MServerConnection::SetLocalInfo(2, EServerType::Login, "LoginSkeleton");

    if (!Session)
    {
        Session = NewMObject<MLoginSession>(this, "Session");
    }
    Session->Initialize(&Sessions, &NextSessionKey);
    return true;
}

void MLoginServer::Tick()
{
}

uint16 MLoginServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MLoginServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    PeerConnections[ConnId] = Conn;
    LOG_INFO("Login skeleton accepted connection %llu", static_cast<unsigned long long>(ConnId));
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

void MLoginServer::ShutdownConnections()
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

void MLoginServer::OnRunStarted()
{
    LOG_INFO("Login skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}

MFuture<TResult<FLoginIssueSessionResponse, FAppError>> MLoginServer::IssueSession(
    const FLoginIssueSessionRequest& Request)
{
    if (!Session)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FLoginIssueSessionResponse>(
            "login_service_missing",
            "IssueSession");
    }

    return Session->IssueSession(Request);
}

MFuture<TResult<FLoginValidateSessionResponse, FAppError>> MLoginServer::ValidateSession(
    const FLoginValidateSessionRequest& Request)
{
    if (!Session)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FLoginValidateSessionResponse>(
            "login_service_missing",
            "ValidateSession");
    }

    return Session->ValidateSession(Request);
}

uint32 MLoginServer::AllocateSessionKey()
{
    return NextSessionKey++;
}

void MLoginServer::HandlePeerPacket(uint64 /*ConnectionId*/, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data)
{
    (void)MServerRpcSupport::DispatchServerCallPacket(this, Connection, Data);
}

