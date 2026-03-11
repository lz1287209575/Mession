#include "GatewayServer.h"
#include "../../Messages/NetMessages.h"
#include <poll.h>

namespace
{
bool IsLoginRoutingMessage(EClientMessageType Type)
{
    return Type == EClientMessageType::MT_Login || Type == EClientMessageType::MT_Handshake;
}

template<typename T>
void AppendValue(TArray& OutData, const T& Value)
{
    const auto* ValueBytes = reinterpret_cast<const uint8*>(&Value);
    OutData.insert(OutData.end(), ValueBytes, ValueBytes + sizeof(T));
}

void AppendString(TArray& OutData, const FString& Value)
{
    const uint16 Length = static_cast<uint16>(Value.size());
    AppendValue(OutData, Length);
    OutData.insert(OutData.end(), Value.begin(), Value.end());
}

template<typename T>
bool ReadValue(const TArray& Data, size_t& Offset, T& OutValue)
{
    if (Offset + sizeof(T) > Data.size())
        return false;

    memcpy(&OutValue, Data.data() + Offset, sizeof(T));
    Offset += sizeof(T);
    return true;
}

bool ReadString(const TArray& Data, size_t& Offset, FString& OutValue)
{
    uint16 Length = 0;
    if (!ReadValue(Data, Offset, Length) || Offset + Length > Data.size())
        return false;

    OutValue.assign(reinterpret_cast<const char*>(Data.data() + Offset), Length);
    Offset += Length;
    return true;
}
}

bool MGatewayServer::Init(int InPort)
{
    Config.ListenPort = static_cast<uint16>(InPort);
    // 创建监听socket
    ListenSocket = MSocket::CreateListenSocket((uint16)InPort);
    
    if (ListenSocket < 0)
    {
        LOG_ERROR("Failed to create listen socket on port %d", InPort);
        return false;
    }
    
    bRunning = true;
    
    printf("=====================================\n");
    printf("  Mession Gateway Server\n");
    printf("  Listening on port %d (fd=%d)\n", InPort, ListenSocket);
    printf("=====================================\n");
    
    // 设置本服务器信息
    MServerConnection::SetLocalInfo(1, EServerType::Gateway, "Gateway01");
    
    // 初始化控制面连接
    SServerConnectionConfig RouterConfig(100, EServerType::Router, "Router01", Config.RouterServerAddr, Config.RouterServerPort);
    RouterServerConn = TSharedPtr<MServerConnection>(new MServerConnection(RouterConfig));

    RouterServerConn->SetOnConnect([](auto) {
        LOG_INFO("Connected to Router Server!");
    });
    RouterServerConn->SetOnAuthenticated([this](auto, const SServerInfo& Info) {
        LOG_INFO("Router Server authenticated: %s", Info.ServerName.c_str());
        SendRouterRegister();
        QueryRoute(EServerType::Login);
        QueryRoute(EServerType::World);
    });
    RouterServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleRouterServerMessage(Type, Data);
    });

    // 初始化后端长连接
    SServerConnectionConfig LoginConfig(2, EServerType::Login, "Login01", "", 0);
    LoginServerConn = TSharedPtr<MServerConnection>(new MServerConnection(LoginConfig));
    
    SServerConnectionConfig WorldConfig(3, EServerType::World, "World01", "", 0);
    WorldServerConn = TSharedPtr<MServerConnection>(new MServerConnection(WorldConfig));
    
    // 设置回调
    LoginServerConn->SetOnConnect([](auto) {
        LOG_INFO("Connected to Login Server!");
    });
    LoginServerConn->SetOnAuthenticated([](auto, const SServerInfo& Info) {
        LOG_INFO("Login Server authenticated: %s", Info.ServerName.c_str());
    });
    LoginServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleLoginServerMessage(Type, Data);
    });
    
    WorldServerConn->SetOnConnect([](auto) {
        LOG_INFO("Connected to World Server!");
    });
    WorldServerConn->SetOnAuthenticated([this](auto, const SServerInfo& Info) {
        LOG_INFO("World Server authenticated: %s", Info.ServerName.c_str());
        FlushPendingWorldLogins();
    });
    WorldServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleWorldServerMessage(Type, Data);
    });
    
    // 优先连接 Router，由 Router 返回当前可用后端地址
    RouterServerConn->Connect();
    
    printf("Backend connections initialized\n");
    
    return true;
}

void MGatewayServer::Shutdown()
{
    if (!bRunning)
        return;
    
    bRunning = false;
    
    // 关闭所有客户端连接
    for (auto& [Id, Conn] : ClientConnections)
    {
        if (Conn->Connection)
            Conn->Connection->Close();
    }
    ClientConnections.clear();
    
    // 关闭后端长连接
    if (RouterServerConn)
        RouterServerConn->Disconnect();
    if (LoginServerConn)
        LoginServerConn->Disconnect();
    if (WorldServerConn)
        WorldServerConn->Disconnect();
    
    // 关闭监听socket
    if (ListenSocket >= 0)
    {
        MSocket::Close(ListenSocket);
        ListenSocket = -1;
    }
    
    LOG_INFO("Gateway server shutdown complete");
}

void MGatewayServer::Tick()
{
    if (!bRunning)
        return;

    static constexpr float BackendTickInterval = 0.016f;
    
    // 接受新客户端
    AcceptClients();

    if (RouterServerConn)
        RouterServerConn->Tick(BackendTickInterval);

    RouteQueryTimer += BackendTickInterval;
    if (RouterServerConn && RouterServerConn->IsConnected() && RouteQueryTimer >= 1.0f)
    {
        RouteQueryTimer = 0.0f;
        if (!LoginServerConn || !LoginServerConn->IsConnected())
            QueryRoute(EServerType::Login);
        if (!WorldServerConn || !WorldServerConn->IsConnected())
            QueryRoute(EServerType::World);
    }
    
    if (LoginServerConn)
        LoginServerConn->Tick(BackendTickInterval);
    if (WorldServerConn)
        WorldServerConn->Tick(BackendTickInterval);

    // 处理客户端消息
    ProcessClientMessages();
}

void MGatewayServer::Run()
{
    if (!bRunning)
    {
        LOG_ERROR("Gateway server not initialized!");
        return;
    }
    
    LOG_INFO("Gateway server running...");
    
    while (bRunning)
    {
        Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

void MGatewayServer::AcceptClients()
{
    TString Address;
    uint16 Port;
    
    int32 ClientSocket = MSocket::Accept(ListenSocket, Address, Port);
    
    while (ClientSocket >= 0)
    {
        uint64 ConnectionId = NextConnectionId++;
        auto Connection = TSharedPtr<MTcpConnection>(new MTcpConnection(ClientSocket));
        Connection->SetNonBlocking(true);
        
        auto Client = TSharedPtr<MClientConnection>(new MClientConnection(ConnectionId, Connection));
        ClientConnections[ConnectionId] = Client;
        
        LOG_INFO("New client connected: %s (connection_id=%llu)", 
                 Address.c_str(), (unsigned long long)ConnectionId);
        
        ClientSocket = MSocket::Accept(ListenSocket, Address, Port);
    }
}

void MGatewayServer::ProcessClientMessages()
{
    TVector<uint64> DisconnectedClients;
    
    TVector<pollfd> PollFds;
    for (auto& [ConnId, Client] : ClientConnections)
    {
        if (Client->Connection->IsConnected())
        {
            Client->Connection->FlushSendBuffer();
            pollfd Pfd;
            Pfd.fd = Client->Connection->GetSocketFd();
            Pfd.events = POLLIN;
            PollFds.push_back(Pfd);
        }
    }
    
    if (PollFds.empty())
        return;
    
    int32 Ret = poll(PollFds.data(), PollFds.size(), 10);
    
    if (Ret < 0)
        return;
    
    size_t Index = 0;
    for (auto& [ConnId, Client] : ClientConnections)
    {
        if (Index >= PollFds.size())
            break;
        
        if (PollFds[Index].revents & POLLIN)
        {
            TArray Packet;
            while (Client->Connection->ReceivePacket(Packet))
            {
                HandleClientPacket(ConnId, Packet);
            }
            
            if (!Client->Connection->IsConnected())
            {
                DisconnectedClients.push_back(ConnId);
            }
        }
        
        Index++;
    }
    
    // 处理断开连接
    for (uint64 ConnId : DisconnectedClients)
    {
        LOG_INFO("Client disconnected: %llu", (unsigned long long)ConnId);
        ClientConnections.erase(ConnId);
    }
}

void MGatewayServer::HandleClientPacket(uint64 ConnectionId, const TArray& Data)
{
    if (Data.empty())
        return;
    
    // 简单处理：直接转发到后端
    // 实际应该解析消息头，判断类型
    
    // 假设第一个字节是消息类型
    const EClientMessageType MsgType = (EClientMessageType)Data[0];
    
    // 登录相关消息转发到LoginServer
    if (IsLoginRoutingMessage(MsgType))
    {
        ForwardToBackend(ConnectionId, Data);
    }
    // 游戏消息转发到WorldServer
    else
    {
        ForwardToBackend(ConnectionId, Data);
    }
}

void MGatewayServer::ForwardToBackend(uint64 ConnectionId, const TArray& Data)
{
    auto ClientIt = ClientConnections.find(ConnectionId);
    if (ClientIt == ClientConnections.end())
        return;

    if (Data.empty())
        return;

    const EClientMessageType MsgType = (EClientMessageType)Data[0];
    if (IsLoginRoutingMessage(MsgType))
    {
        if (!LoginServerConn || !LoginServerConn->IsConnected())
        {
            LOG_WARN("Login server unavailable, dropping login request");
            return;
        }

        if (Data.size() < 1 + sizeof(uint64))
        {
            LOG_WARN("Invalid client login packet size: %zu", Data.size());
            return;
        }

        uint64 PlayerId = 0;
        memcpy(&PlayerId, Data.data() + 1, sizeof(PlayerId));

        TArray Payload;
        AppendValue(Payload, ConnectionId);
        AppendValue(Payload, PlayerId);
        LoginServerConn->Send((uint8)EServerMessageType::MT_PlayerLogin, Payload.data(), Payload.size());
        LOG_DEBUG("Forwarded login request for player %llu", (unsigned long long)PlayerId);
        return;
    }

    if (!ClientIt->second->bAuthenticated)
    {
        LOG_WARN("Ignoring unauthenticated client message type %d", (int)MsgType);
        return;
    }

    if (!WorldServerConn || !WorldServerConn->IsConnected())
    {
        LOG_WARN("World server unavailable, dropping client message type %d", (int)MsgType);
        return;
    }

    TArray Payload;
    AppendValue(Payload, ConnectionId);

    const uint32 DataSize = static_cast<uint32>(Data.size());
    AppendValue(Payload, DataSize);
    Payload.insert(Payload.end(), Data.begin(), Data.end());

    WorldServerConn->Send((uint8)EServerMessageType::MT_PlayerDataSync, Payload.data(), Payload.size());
    LOG_DEBUG("Forwarded client message type %d to WorldServer", (int)MsgType);
}

void MGatewayServer::HandleLoginServerMessage(uint8 Type, const TArray& Data)
{
    if (Type != (uint8)EServerMessageType::MT_PlayerLogin)
        return;

    size_t Offset = 0;
    uint64 ConnectionId = 0;
    uint64 PlayerId = 0;
    uint32 SessionKey = 0;
    if (!ReadValue(Data, Offset, ConnectionId) ||
        !ReadValue(Data, Offset, PlayerId) ||
        !ReadValue(Data, Offset, SessionKey))
    {
        LOG_WARN("Invalid login server payload size: %zu", Data.size());
        return;
    }

    auto ClientIt = ClientConnections.find(ConnectionId);
    if (ClientIt == ClientConnections.end())
    {
        LOG_WARN("Login response for unknown client %llu", (unsigned long long)ConnectionId);
        return;
    }

    auto& Client = ClientIt->second;
    Client->PlayerId = PlayerId;
    Client->SessionToken = SessionKey;
    Client->bAuthenticated = true;

    TArray Response;
    Response.resize(1 + sizeof(SessionKey) + sizeof(PlayerId));
    Response[0] = (uint8)EClientMessageType::MT_LoginResponse;
    memcpy(Response.data() + 1, &SessionKey, sizeof(SessionKey));
    memcpy(Response.data() + 1 + sizeof(SessionKey), &PlayerId, sizeof(PlayerId));
    Client->Connection->Send(Response.data(), Response.size());

    const uint64 RouteRequestId = QueryRoute(EServerType::World, PlayerId);
    if (RouteRequestId != 0)
        PendingWorldLoginRoutes[RouteRequestId] = {ConnectionId, PlayerId, SessionKey};

    LOG_INFO("Client %llu authenticated as player %llu",
             (unsigned long long)ConnectionId,
             (unsigned long long)PlayerId);
}

void MGatewayServer::HandleWorldServerMessage(uint8 Type, const TArray& Data)
{
    if (Type == (uint8)EServerMessageType::MT_PlayerLogout)
    {
        size_t Offset = 0;
        uint64 ConnectionId = 0;
        if (!ReadValue(Data, Offset, ConnectionId))
            return;

        auto ClientIt = ClientConnections.find(ConnectionId);
        if (ClientIt != ClientConnections.end())
        {
            ClientIt->second->bAuthenticated = false;
            ClientIt->second->SessionToken = 0;
        }
    }
}

void MGatewayServer::HandleRouterServerMessage(uint8 Type, const TArray& Data)
{
    switch ((EServerMessageType)Type)
    {
        case EServerMessageType::MT_ServerRegisterAck:
            LOG_INFO("Gateway registered to RouterServer");
            break;

        case EServerMessageType::MT_RouteResponse:
        {
            size_t Offset = 0;
            uint64 RequestId = 0;
            uint8 RequestedTypeValue = 0;
            uint64 PlayerId = 0;
            uint8 Result = 0;
            if (!ReadValue(Data, Offset, RequestId) ||
                !ReadValue(Data, Offset, RequestedTypeValue) ||
                !ReadValue(Data, Offset, PlayerId) ||
                !ReadValue(Data, Offset, Result))
            {
                LOG_WARN("Invalid route response payload size: %zu", Data.size());
                return;
            }

            const EServerType RequestedType = (EServerType)RequestedTypeValue;
            if (!Result)
            {
                LOG_WARN("No route available yet for server type %d (request=%llu)",
                         (int)RequestedType,
                         (unsigned long long)RequestId);
                return;
            }

            uint32 ServerId = 0;
            uint8 ServerTypeValue = 0;
            FString ServerName;
            FString Address;
            uint16 Port = 0;
            if (!ReadValue(Data, Offset, ServerId) ||
                !ReadValue(Data, Offset, ServerTypeValue) ||
                !ReadString(Data, Offset, ServerName) ||
                !ReadString(Data, Offset, Address) ||
                !ReadValue(Data, Offset, Port))
            {
                LOG_WARN("Invalid successful route response payload size: %zu", Data.size());
                return;
            }

            ApplyRoute((EServerType)ServerTypeValue, ServerId, ServerName, Address, Port);
            if ((EServerType)ServerTypeValue == EServerType::World)
            {
                auto PendingIt = PendingWorldLoginRoutes.find(RequestId);
                if (PendingIt != PendingWorldLoginRoutes.end())
                    FlushPendingWorldLogins();
                else if (PlayerId == 0)
                    FlushPendingWorldLogins();
            }
            break;
        }

        default:
            break;
    }
}

void MGatewayServer::SendRouterRegister()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
        return;

    TArray Payload;
    AppendValue(Payload, static_cast<uint32>(1));
    Payload.push_back((uint8)EServerType::Gateway);
    AppendString(Payload, "Gateway01");
    AppendString(Payload, "127.0.0.1");
    AppendValue(Payload, Config.ListenPort);
    RouterServerConn->Send((uint8)EServerMessageType::MT_ServerRegister, Payload.data(), Payload.size());
}

uint64 MGatewayServer::QueryRoute(EServerType ServerType, uint64 PlayerId)
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
        return 0;

    const uint64 RequestId = NextRouteRequestId++;
    TArray Payload;
    AppendValue(Payload, RequestId);
    Payload.push_back((uint8)ServerType);
    AppendValue(Payload, PlayerId);
    RouterServerConn->Send((uint8)EServerMessageType::MT_RouteQuery, Payload.data(), Payload.size());
    return RequestId;
}

void MGatewayServer::ApplyRoute(EServerType ServerType, uint32 ServerId, const FString& ServerName, const FString& Address, uint16 Port)
{
    TSharedPtr<MServerConnection> TargetConn;
    if (ServerType == EServerType::Login)
        TargetConn = LoginServerConn;
    else if (ServerType == EServerType::World)
        TargetConn = WorldServerConn;
    else
        return;

    if (!TargetConn)
        return;

    const SServerConnectionConfig& CurrentConfig = TargetConn->GetConfig();
    const bool bRouteChanged =
        CurrentConfig.ServerId != ServerId ||
        CurrentConfig.ServerType != ServerType ||
        CurrentConfig.ServerName != ServerName ||
        CurrentConfig.Address != Address ||
        CurrentConfig.Port != Port;

    if (bRouteChanged && (TargetConn->IsConnected() || TargetConn->IsConnecting()))
        TargetConn->Disconnect();

    SServerConnectionConfig NewConfig(ServerId, ServerType, ServerName, Address, Port);
    NewConfig.HeartbeatInterval = CurrentConfig.HeartbeatInterval;
    NewConfig.ConnectTimeout = CurrentConfig.ConnectTimeout;
    NewConfig.ReconnectInterval = CurrentConfig.ReconnectInterval;
    TargetConn->SetConfig(NewConfig);

    if (!TargetConn->IsConnected() && !TargetConn->IsConnecting())
        TargetConn->Connect();

    if (ServerType == EServerType::World && TargetConn->IsConnected())
        FlushPendingWorldLogins();
}

void MGatewayServer::FlushPendingWorldLogins()
{
    if (!WorldServerConn || !WorldServerConn->IsConnected())
        return;

    TVector<uint64> CompletedRequests;
    for (const auto& [RequestId, Pending] : PendingWorldLoginRoutes)
    {
        TArray Payload;
        AppendValue(Payload, Pending.ConnectionId);
        AppendValue(Payload, Pending.PlayerId);
        AppendValue(Payload, Pending.SessionKey);
        WorldServerConn->Send((uint8)EServerMessageType::MT_PlayerLogin, Payload.data(), Payload.size());
        CompletedRequests.push_back(RequestId);
    }

    for (uint64 RequestId : CompletedRequests)
        PendingWorldLoginRoutes.erase(RequestId);
}
