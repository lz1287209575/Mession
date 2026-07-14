#include "Servers/Gateway/GatewayServer.h"
#include "Servers/App/ServiceMain.h"
#include "Common/Net/Rpc/ClientManifest.h"
#include "Common/Net/Rpc/MRpcChannel.h"
#include "Common/Net/Rpc/RpcClientCall.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/Rpc/RpcPayload.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Servers/App/ServerCallAsyncSupport.h"
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

bool MGatewayServer::Init(int InPort)
{
    // MService<SGatewayConfig>::LoadConfig(argc, argv) 已经在 main 入口
    // （ServiceMain.h::Run 模板）跑过，这里直接从单例拷一份即可。
    Config = MService<SGatewayConfig>::GetConfig();

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

        // 注册到本进程 MServerRuntimeContext（用作 RpcRuntimeContext::ResolveServerTransport）
        RegisterRpcTransport(Peer.ServerType, PeerConn);

        LOG_INFO("GatewayServer: connected to peer %s at %s:%u",
                 GetServerTypeDisplayName(Peer.ServerType),
                 Peer.Address.c_str(),
                 static_cast<unsigned>(Peer.Port));
    }
}

void MGatewayServer::Tick()
{
    for (const auto& Pair : GetRpcTransports())
    {
        if (Pair.second)
        {
            Pair.second->Tick(0.0f);
        }
    }
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
            LOG_INFO("Gateway received client packet: conn=%llu size=%zu",
                     static_cast<unsigned long long>(ConnectionId), Payload.size());
            HandleClientPacket(ConnectionId, Payload);
        },
        [this](uint64 ConnectionId)
        {
            ClientConnections.erase(ConnectionId);
        });
}

void MGatewayServer::TickBackends()
{
    for (const auto& Pair : GetRpcTransports())
    {
        if (Pair.second)
        {
            Pair.second->Tick(0.1f);
        }
    }
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
    ClearRpcTransports();
}

void MGatewayServer::OnRunStarted()
{
    LOG_INFO("Gateway skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
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

MFuture<TResult<SEmptyServerMessage, FAppError>> MGatewayServer::Rpc_OnServerHandshake(uint32 /*DummyServerId*/)
{
    return MServerCallAsyncSupport::MakeSuccessFuture(SEmptyServerMessage{});
}

MFuture<TResult<SEmptyServerMessage, FAppError>> MGatewayServer::Rpc_OnHeartbeat(uint32 /*Seq*/)
{
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

    const MClientManifest::SEntry* Entry = MClientManifest::FindByFunctionId(FunctionId);

    // PoC 阶段：MFUNCTION(Client/ClientCall) 还没声明，MClientManifest 是空表。
    // 退化路径：所有 Client_* 走当前唯一的业务 Service（MEchoService::Echo）。
    const char* TargetClassName = "MEchoService";
    const char* TargetMethodName = "Echo";
    EServerType TargetServer = EServerType::Echo;
    if (Entry)
    {
        TargetClassName = Entry->OwnerType;
        TargetMethodName = Entry->FunctionName;
        TargetServer = GetGlobalClientFunctionTargetServerType(FunctionId);
    }

    // Gateway 收到 Client_* 后把它当作 ServerCall 转给对应的 Service 实例。
    // Service 实例通过 TargetActorId 走 MRpcChannel::CallToActor 自己寻址。
    FSampleEchoRequest ServiceRequest;
    ServiceRequest.TargetActorId = 0;
    ServiceRequest.Message = MString();

    // PoC 第 1 步：所有 Client_* 都假定包含 8 字节 ActorId 头部 + 后续 message body
    if (Payload.size() < sizeof(uint64))
    {
        LOG_WARN("Gateway: actor_id_required for function=%u", static_cast<unsigned>(FunctionId));
        return;
    }
    std::memcpy(&ServiceRequest.TargetActorId, Payload.data(), sizeof(uint64));
    ServiceRequest.Message.assign(
        reinterpret_cast<const char*>(Payload.data() + sizeof(uint64)),
        Payload.size() - sizeof(uint64));

    FClientCallHandle Handle(FunctionId, ConnectionId, CallId);

    // 通过 MRpcChannel::Call 走 Resolver 路由到目标 Service
    MRpcChannel::Get().Call<FSampleEchoResponse>(
        this,
        TargetServer,
        TargetClassName,
        TargetMethodName,
        ServiceRequest)
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

// CreateService 工厂——ServiceMain.h 中 namespace 内的
// CreateService<TService>() 模板已经用 NewMObject<TService>() 兜底
// 实例化。如果未来某 Service 想要更精细的初始化（例如注入外部依赖），
// 可以在此文件加特殊的 extern "C" 工厂并相应去掉兜底调用——目前不需要。