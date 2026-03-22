#include "Servers/Gateway/GatewayServer.h"
#include "Servers/App/ClientCallAsyncSupport.h"
#include "Servers/App/GatewayClientFlows.h"
#include "Servers/App/ServerRpcSupport.h"

const MClass* MGatewayServer::GetLoginServerClass() const
{
    return MObject::FindClass("MLoginServer");
}

const MClass* MGatewayServer::GetWorldServerClass() const
{
    return MObject::FindClass("MWorldServer");
}

bool MGatewayServer::LoadConfig(const MString& /*ConfigPath*/)
{
    return true;
}

bool MGatewayServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    bRunning = true;
    MLogger::LogStartupBanner("GatewayServer", Config.ListenPort, 0);
    MServerConnection::SetLocalInfo(1, EServerType::Gateway, "GatewaySkeleton");

    LoginServerConn = BackendConnectionManager.AddServer(
        SServerConnectionConfig(2, EServerType::Login, "LoginSkeleton", Config.LoginServerAddr, Config.LoginServerPort));
    WorldServerConn = BackendConnectionManager.AddServer(
        SServerConnectionConfig(3, EServerType::World, "WorldSkeleton", Config.WorldServerAddr, Config.WorldServerPort));

    LoginServerConn->SetOnMessage([this](auto, uint8 PacketType, const TByteArray& Data)
    {
        HandleBackendPacket(PacketType, Data, "Login");
    });
    WorldServerConn->SetOnMessage([this](auto, uint8 PacketType, const TByteArray& Data)
    {
        HandleBackendPacket(PacketType, Data, "World");
    });

    LoginServerConn->Connect();
    WorldServerConn->Connect();
    return true;
}

void MGatewayServer::Tick()
{
}

uint16 MGatewayServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MGatewayServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    ClientConnections[ConnId] = Conn;
    LOG_INFO("Gateway skeleton accepted connection %llu", static_cast<unsigned long long>(ConnId));
    EventLoop.RegisterConnection(
        ConnId,
        Conn,
        [this](uint64 ConnectionId, const TByteArray& Payload)
        {
            HandleClientPacket(ConnectionId, Payload);
        },
        [this](uint64 ConnectionId)
        {
            ClientConnections.erase(ConnectionId);
        });
}

void MGatewayServer::TickBackends()
{
    BackendConnectionManager.Tick(0.1f);
}

void MGatewayServer::ShutdownConnections()
{
    for (auto& [ConnId, Conn] : ClientConnections)
    {
        (void)ConnId;
        if (Conn)
        {
            Conn->Close();
        }
    }
    ClientConnections.clear();
    BackendConnectionManager.DisconnectAll();
    LoginServerConn.reset();
    WorldServerConn.reset();
}

void MGatewayServer::OnRunStarted()
{
    LOG_INFO("Gateway skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}

bool MGatewayServer::SendGeneratedClientResponse(uint64 ConnectionId, uint16 FunctionId, uint64 CallId, const TByteArray& Payload)
{
    auto It = ClientConnections.find(ConnectionId);
    if (It == ClientConnections.end() || !It->second)
    {
        return false;
    }

    TByteArray Packet;
    if (!BuildClientCallPacket(FunctionId, CallId, Payload, Packet))
    {
        return false;
    }

    return It->second->Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

void MGatewayServer::Client_Echo(FClientEchoRequest& Request, FClientEchoResponse& Response)
{
    Response.ConnectionId = GetCurrentClientConnectionId();
    Response.Message = Request.Message;
}

void MGatewayServer::Client_Login(FClientLoginRequest& Request, FClientLoginResponse& Response)
{
    const uint64 GatewayConnectionId = GetCurrentClientConnectionId();
    if (GatewayConnectionId == 0)
    {
        Response.Error = "client_context_missing";
        return;
    }

    const SGeneratedClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    SGatewayClientFlowDeps FlowDeps;
    FlowDeps.LoginServerConn = LoginServerConn;
    FlowDeps.WorldServerConn = WorldServerConn;
    FlowDeps.LoginServerClass = GetLoginServerClass();
    FlowDeps.WorldServerClass = GetWorldServerClass();

    (void)MClientCallAsyncSupport::StartDeferred<FClientLoginResponse>(
        Context,
        MGatewayClientFlows::StartLogin(FlowDeps, Request, GatewayConnectionId),
        [](const FAppError& Error)
        {
            FClientLoginResponse Failed;
            Failed.Error = Error.Code.empty() ? "client_login_failed" : Error.Code;
            return Failed;
        });
}

void MGatewayServer::Client_FindPlayer(FClientFindPlayerRequest& Request, FClientFindPlayerResponse& Response)
{
    const SGeneratedClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    SGatewayClientFlowDeps FlowDeps;
    FlowDeps.WorldServerConn = WorldServerConn;
    FlowDeps.WorldServerClass = GetWorldServerClass();

    (void)MClientCallAsyncSupport::StartDeferred<FClientFindPlayerResponse>(
        Context,
        MGatewayClientFlows::StartFindPlayer(FlowDeps, Request),
        [](const FAppError& Error)
        {
            FClientFindPlayerResponse Failed;
            Failed.Error = Error.Code.empty() ? "player_find_failed" : Error.Code;
            return Failed;
        });
}

void MGatewayServer::Client_Logout(FClientLogoutRequest& Request, FClientLogoutResponse& Response)
{
    const SGeneratedClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    SGatewayClientFlowDeps FlowDeps;
    FlowDeps.WorldServerConn = WorldServerConn;
    FlowDeps.WorldServerClass = GetWorldServerClass();

    (void)MClientCallAsyncSupport::StartDeferred<FClientLogoutResponse>(
        Context,
        MGatewayClientFlows::StartLogout(FlowDeps, Request),
        [](const FAppError& Error)
        {
            FClientLogoutResponse Failed;
            Failed.Error = Error.Code.empty() ? "player_logout_failed" : Error.Code;
            return Failed;
        });
}

void MGatewayServer::Client_SwitchScene(FClientSwitchSceneRequest& Request, FClientSwitchSceneResponse& Response)
{
    const SGeneratedClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    SGatewayClientFlowDeps FlowDeps;
    FlowDeps.WorldServerConn = WorldServerConn;
    FlowDeps.WorldServerClass = GetWorldServerClass();

    (void)MClientCallAsyncSupport::StartDeferred<FClientSwitchSceneResponse>(
        Context,
        MGatewayClientFlows::StartSwitchScene(FlowDeps, Request),
        [](const FAppError& Error)
        {
            FClientSwitchSceneResponse Failed;
            Failed.Error = Error.Code.empty() ? "player_switch_scene_failed" : Error.Code;
            return Failed;
        });
}

void MGatewayServer::HandleClientPacket(uint64 ConnectionId, const TByteArray& Data)
{
    uint16 FunctionId = 0;
    uint64 CallId = 0;
    uint32 PayloadSize = 0;
    size_t PayloadOffset = 0;
    if (!ParseClientCallPacket(Data, FunctionId, CallId, PayloadSize, PayloadOffset))
    {
        LOG_WARN("Gateway client packet parse failed: connection=%llu", static_cast<unsigned long long>(ConnectionId));
        return;
    }

    TByteArray Payload;
    if (PayloadSize > 0)
    {
        Payload.insert(
            Payload.end(),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    (void)DispatchGeneratedClientFunction(this, ConnectionId, FunctionId, CallId, Payload);
}

void MGatewayServer::HandleBackendPacket(uint8 PacketType, const TByteArray& Data, const char* PeerName)
{
    if (PacketType == static_cast<uint8>(EServerMessageType::MT_FunctionResponse))
    {
        if (!HandleGeneratedServerCallResponse(Data))
        {
            LOG_WARN("Gateway failed to handle backend function response from %s", PeerName ? PeerName : "backend");
        }
        return;
    }

    LOG_WARN("Gateway received unsupported backend packet from %s: type=%u",
             PeerName ? PeerName : "backend",
             static_cast<unsigned>(PacketType));
}
