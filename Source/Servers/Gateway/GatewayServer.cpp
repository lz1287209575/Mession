#include "Servers/Gateway/GatewayServer.h"
#include "Common/Net/Rpc/ClientManifest.h"
#include "Common/Net/Rpc/RpcClientCall.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/Rpc/RpcPayload.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/App/ServiceContainer.h"
#include "Servers/App/ServiceId.h"

MGatewayServer* GGlobalGateway = nullptr;

namespace
{
struct FClientCallHandle
{
    uint16 FunctionId = 0;
    uint64 ConnectionId = 0;
    uint64 CallId = 0;
};

bool DispatchBackendServerCallPacket(
    MObject* Service,
    const TSharedPtr<MServerConnection>& Connection,
    const TByteArray& Data)
{
    if (!Service || !Connection || Data.empty())
    {
        return false;
    }

    uint16 FunctionId = 0;
    uint64 CallId = 0;
    uint32 PayloadSize = 0;
    size_t PayloadOffset = 0;
    if (!ParseServerCallPacket(Data, FunctionId, CallId, PayloadSize, PayloadOffset))
    {
        return false;
    }

    TByteArray RequestPayload;
    if (PayloadSize > 0)
    {
        RequestPayload.insert(
            RequestPayload.end(),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    const TSharedPtr<IServerCallResponseTarget> ResponseTarget =
        MakeShared<MServerCallResponseTarget>(
            [Connection]() -> bool
            {
                return Connection && Connection->IsConnected();
            },
            [Connection](uint16 ResponseFunctionId, uint64 ResponseCallId, bool bSuccess, const TByteArray& ResponsePayload) -> bool
            {
                TByteArray ResponsePacketPayload;
                if (!BuildServerCallResponsePacket(
                        ResponseFunctionId,
                        ResponseCallId,
                        bSuccess,
                        ResponsePayload,
                        ResponsePacketPayload))
                {
                    return false;
                }

                return SendServerCallResponseMessage(Connection, ResponsePacketPayload);
            });

    return DispatchServerCall(Service, FunctionId, CallId, RequestPayload, ResponseTarget);
}

// PoC：把内嵌的 Then lambda 抽出来
MFuture<TResult<SEmptyServerMessage, FAppError>> HandleEchoResult(
    MFuture<TResult<FSampleEchoResponse, FAppError>> Inner,
    FClientCallHandle Handle)
{
    TResult<SEmptyServerMessage, FAppError> OutResult;
    try
    {
        const TResult<FSampleEchoResponse, FAppError> InnerResult = Inner.Get();
        if (InnerResult.IsErr())
        {
            OutResult = TResult<SEmptyServerMessage, FAppError>::Err(InnerResult.GetError());
        }
        else
        {
            // 把 FSampleEchoResponse 序列化后通过 PushClientDownlink 发回 UE
            const FSampleEchoResponse& Resp = InnerResult.GetValue();
            TByteArray Payload = BuildPayload(Resp);

            MGatewayServer* Gateway = MGatewayServer::GetSingleton();
            if (Gateway)
            {
                Gateway->PushClientDownlink(FClientDownlinkPushRequest{Handle.ConnectionId, Handle.FunctionId, Payload});
            }

            OutResult = TResult<SEmptyServerMessage, FAppError>::Ok(SEmptyServerMessage{});
        }
    }
    catch (const std::exception& Ex)
    {
        OutResult = TResult<SEmptyServerMessage, FAppError>::Err(
            FAppError::Make("handle_echo_exception", Ex.what()));
    }

    MPromise<TResult<SEmptyServerMessage, FAppError>> Promise;
    Promise.SetValue(std::move(OutResult));
    return Promise.GetFuture();
}
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
    GGlobalGateway = this;
    MLogger::LogStartupBanner("GatewayServer", Config.ListenPort, 0);
    MServerConnection::SetLocalInfo(1, EServerType::Gateway, "GatewaySkeleton");

    ConnectAllPeers();

    return true;
}

void MGatewayServer::ConnectAllPeers()
{
    for (const SServicePeerConfig& Peer : Config.Peers)
    {
        const uint32 PeerServerId = MUniqueIdGenerator::Generate();
        const SServerConnectionConfig PeerConfig(
            PeerServerId,
            Peer.ServerType,
            GetServerTypeDisplayName(Peer.ServerType),
            Peer.Address,
            Peer.Port);

        TSharedPtr<MServerConnection> PeerConn = MakeShared<MServerConnection>(PeerConfig);
        PeerConn->SetOnMessage([this](auto Connection, uint8 PacketType, const TByteArray& Data)
        {
            HandleBackendPacket(Connection, PacketType, Data, GetServerTypeDisplayName(Connection->GetConfig().ServerType));
        });
        PeerConn->Connect();

        // 1. 注册到 MServiceContainer（全局 transport map）
        MServiceContainer::Get().Register(PeerConn);

        // 2. 注册到本进程 MServerRuntimeContext
        RegisterRpcTransport(Peer.ServerType, PeerConn);

        LOG_INFO("GatewayServer: connected to peer %s at %s:%u",
                 GetServerTypeDisplayName(Peer.ServerType),
                 Peer.Address.c_str(),
                 static_cast<unsigned>(Peer.Port));
    }
}

void MGatewayServer::Tick()
{
    MServiceContainer::Get().TickAll(0.0f);
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
    MServiceContainer::Get().TickAll(0.1f);
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
    MServiceContainer::Get().ShutdownAll();
    ClearRpcTransports();
}

void MGatewayServer::OnRunStarted()
{
    LOG_INFO("Gateway skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}

void MGatewayServer::Client_Echo(FClientEchoRequest& Request, FClientEchoResponse& Response)
{
    Response.ConnectionId = GetCurrentClientConnectionId();
    Response.Message = Request.Message;
}

void MGatewayServer::Client_Heartbeat(FClientHeartbeatRequest& Request, FClientHeartbeatResponse& Response)
{
    Response.bSuccess = true;
    Response.Sequence = Request.Sequence;
    Response.ConnectionId = GetCurrentClientConnectionId();
}

MFuture<TResult<SEmptyServerMessage, FAppError>> MGatewayServer::PushClientDownlink(
    const FClientDownlinkPushRequest& Request)
{
    if (Request.GatewayConnectionId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>(
            "gateway_connection_id_required",
            "PushClientDownlink");
    }

    if (Request.FunctionId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>(
            "client_downlink_function_required",
            "PushClientDownlink");
    }

    const auto It = ClientConnections.find(Request.GatewayConnectionId);
    if (It == ClientConnections.end() || !It->second || !It->second->IsConnected())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>(
            "gateway_client_connection_missing",
            "PushClientDownlink");
    }

    TByteArray Packet;
    if (!BuildClientFunctionPacket(Request.FunctionId, Request.Payload, Packet))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>(
            "client_downlink_packet_build_failed",
            "PushClientDownlink");
    }

    if (!It->second->Send(Packet.data(), static_cast<uint32>(Packet.size())))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>(
            "client_downlink_send_failed",
            "PushClientDownlink");
    }

    return MServerCallAsyncSupport::MakeSuccessFuture(SEmptyServerMessage{});
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

    const FClientFunctionRoute* Route = MClientFunctionRouteTable::FindRoute(FunctionId);
    if (!Route)
    {
        LOG_WARN("Gateway received unknown client function id=%u connection=%llu",
                 static_cast<unsigned>(FunctionId),
                 static_cast<unsigned long long>(ConnectionId));
        return;
    }

    // 通过 MServiceContainer 查 peer transport
    TSharedPtr<MServerConnection> TargetConn = MServiceContainer::Get().Resolve(Route->TargetServiceType);
    if (!TargetConn || !TargetConn->IsConnected())
    {
        LOG_WARN("Gateway: peer transport unavailable for function=%u target=%s",
                 static_cast<unsigned>(FunctionId),
                 GetServerTypeDisplayName(Route->TargetServiceType));
        return;
    }

    // PoC 第 1 步：仅 Echo 一条路由 + bRequiresActorId=true
    FSampleEchoRequest ServiceRequest;
    ServiceRequest.TargetActorId = 0;
    ServiceRequest.Message = MString();

    if (Route->bRequiresActorId)
    {
        if (Payload.size() < sizeof(uint64))
        {
            LOG_WARN("Gateway: actor_id_required for function=%u", static_cast<unsigned>(FunctionId));
            return;
        }
        std::memcpy(&ServiceRequest.TargetActorId, Payload.data(), sizeof(uint64));
        ServiceRequest.Message.assign(
            reinterpret_cast<const char*>(Payload.data() + sizeof(uint64)),
            Payload.size() - sizeof(uint64));
    }
    else
    {
        ServiceRequest.Message.assign(
            reinterpret_cast<const char*>(Payload.data()),
            Payload.size());
    }

    FClientCallHandle Handle(FunctionId, ConnectionId, CallId);
    CallServerFunction<FSampleEchoResponse>(TargetConn, Route->ClassName, Route->MethodName, ServiceRequest)
        .Then([Handle](MFuture<TResult<FSampleEchoResponse, FAppError>> Completed)
        {
            return HandleEchoResult(std::move(Completed), Handle);
        });
}

void MGatewayServer::HandleBackendPacket(
    const TSharedPtr<MServerConnection>& Connection,
    uint8 PacketType,
    const TByteArray& Data,
    const char* PeerName)
{
    if (PacketType == static_cast<uint8>(EServerMessageType::MT_FunctionCall))
    {
        if (!DispatchBackendServerCallPacket(this, Connection, Data))
        {
            LOG_WARN("Gateway failed to dispatch backend function call from %s", PeerName ? PeerName : "backend");
        }
        return;
    }

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