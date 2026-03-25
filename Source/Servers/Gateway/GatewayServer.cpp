#include "Servers/Gateway/GatewayServer.h"
#include "Servers/App/ServerRpcSupport.h"
#include "Common/Net/Rpc/RpcClientCall.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Runtime/Object/Object.h"

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

    if (!LoginRpc)
    {
        LoginRpc = NewMObject<MGatewayLoginRpc>(this, "LoginRpc");
    }
    if (!WorldRpc)
    {
        WorldRpc = NewMObject<MGatewayWorldRpc>(this, "WorldRpc");
    }
    RegisterRpcTransport(EServerType::Login, LoginServerConn);
    RegisterRpcTransport(EServerType::World, WorldServerConn);
    if (!ClientService)
    {
        ClientService = NewMObject<MGatewayClientServiceEndpoint>(this, "ClientService");
    }
    ClientService->Initialize(LoginRpc, WorldRpc);

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
    ClearRpcTransports();
    LoginServerConn.reset();
    WorldServerConn.reset();
}

void MGatewayServer::OnRunStarted()
{
    LOG_INFO("Gateway skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}

void MGatewayServer::Client_Echo(FClientEchoRequest& Request, FClientEchoResponse& Response)
{
    if (!ClientService)
    {
        Response.Message = "client_service_missing";
        return;
    }

    ClientService->Client_Echo(Request, Response);
}

void MGatewayServer::Client_Login(FClientLoginRequest& Request, FClientLoginResponse& Response)
{
    if (!ClientService)
    {
        Response.Error = "client_service_missing";
        return;
    }

    ClientService->Client_Login(Request, Response);
}

void MGatewayServer::Client_FindPlayer(FClientFindPlayerRequest& Request, FClientFindPlayerResponse& Response)
{
    if (!ClientService)
    {
        Response.Error = "client_service_missing";
        return;
    }

    ClientService->Client_FindPlayer(Request, Response);
}

void MGatewayServer::Client_Logout(FClientLogoutRequest& Request, FClientLogoutResponse& Response)
{
    if (!ClientService)
    {
        Response.Error = "client_service_missing";
        return;
    }

    ClientService->Client_Logout(Request, Response);
}

void MGatewayServer::Client_SwitchScene(FClientSwitchSceneRequest& Request, FClientSwitchSceneResponse& Response)
{
    if (!ClientService)
    {
        Response.Error = "client_service_missing";
        return;
    }

    ClientService->Client_SwitchScene(Request, Response);
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

    auto ConnIt = ClientConnections.find(ConnectionId);
    if (ConnIt == ClientConnections.end() || !ConnIt->second)
    {
        LOG_WARN("Gateway missing client connection for client call: connection=%llu",
                 static_cast<unsigned long long>(ConnectionId));
        return;
    }

    const TSharedPtr<INetConnection> ClientConnection = ConnIt->second;
    const TSharedPtr<IClientResponseTarget> ResponseTarget =
        MakeShared<MClientResponseTarget>(
            [ClientConnection](uint64 ExpectedConnectionId) -> bool
            {
                (void)ExpectedConnectionId;
                return ClientConnection && ClientConnection->IsConnected();
            },
            [ClientConnection](uint64 ExpectedConnectionId, uint16 ResponseFunctionId, uint64 ResponseCallId, const TByteArray& ResponsePayload) -> bool
            {
                (void)ExpectedConnectionId;
                TByteArray Packet;
                if (!BuildClientCallPacket(ResponseFunctionId, ResponseCallId, ResponsePayload, Packet))
                {
                    return false;
                }

                return ClientConnection->Send(Packet.data(), static_cast<uint32>(Packet.size()));
            });

    (void)DispatchClientFunction(this, ConnectionId, FunctionId, CallId, Payload, ResponseTarget);
}

void MGatewayServer::HandleBackendPacket(uint8 PacketType, const TByteArray& Data, const char* PeerName)
{
    if (PacketType == static_cast<uint8>(EServerMessageType::MT_FunctionResponse))
    {
        if (!HandleServerCallResponse(Data))
        {
            LOG_WARN("Gateway failed to handle backend function response from %s", PeerName ? PeerName : "backend");
        }
        return;
    }

    LOG_WARN("Gateway received unsupported backend packet from %s: type=%u",
             PeerName ? PeerName : "backend",
             static_cast<unsigned>(PacketType));
}
