#include "Servers/Gateway/GatewayServer.h"
#include "Common/Net/ClientCall/MClientTargetContextGuard.h"
#include "Common/Net/ClientCall/MClientTargetResolver.h"
#include "Common/Net/Rpc/MClientManifest.generated.h"
#include "Common/Net/Rpc/MRpcChannel.h"
#include "Common/Net/Rpc/RpcClientCall.h"
#include "Common/Net/Rpc/RpcPayload.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/Rpc/RpcTransport.h"
#include "Common/Net/ServiceDiscovery/Endpoint.h"
#include "Common/Net/ServiceDiscovery/EndpointCache.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Id.h"
#include "Common/Runtime/Object/Object.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/App/ServiceId.h"
#include "Servers/App/ServiceMain.h"

MGatewayServer* GGlobalGateway = nullptr;

bool MGatewayServer::Init(int InPort) {
    // MService<SGatewayConfig>::LoadConfig(argc, argv) 已经在 main 入口
    // （ServiceMain.h::Run 模板）跑过，这里直接从单例拷一份即可。
    Config = MService<SGatewayConfig>::GetConfig();

    if (InPort > 0) {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    if (Config.RegistryAddr.empty()) {
        LOG_ERROR("MGatewayServer: --registry=<host:port> is required");
        return false;
    }

    bRunning       = true;
    GGlobalGateway = this;
    CORE_LOG(Info, ":: Mession Game Server :: (v%s)", "1.0.0");
    NET_LOG(Info, "Starting GatewayServer on port %u", static_cast<unsigned>(Config.ListenPort));

    // 本地 ServerId 没传 → 默认 1（PoC 阶段 Gateway 在 Listener 上是 1）
    const uint32 LocalServerId = 1;
    MServerConnection::SetLocalInfo(LocalServerId, EServerType::Gateway, "GatewaySkeleton");
    Config.LocalServerType = EServerType::Gateway;
    Config.LocalServerId   = LocalServerId;

    // 注册到 Registry：AttachEventLoop → BindRegistry → RegisterLocal
    MEndpointCache::Get().AttachEventLoop(&EventLoop);
    MEndpointCache::Get().SetServiceInstance(this); // P2: install business dispatch target
    MString RegHost;
    uint16  RegPort = 0;
    if (!ParseAddrPort(Config.RegistryAddr, RegHost, RegPort)) {
        LOG_ERROR("MGatewayServer: malformed --registry=%s (expected host:port)", Config.RegistryAddr.c_str());
        return false;
    }
    MEndpointCache::Get().BindRegistry(RegHost, RegPort);
    MEndpointCache::Get().RegisterLocal(MakeLocalEndpoint(Config));

    return true;
}

void MGatewayServer::Tick() {
    MEndpointCache::Get().Tick(0.0f);
}

uint16 MGatewayServer::GetListenPort() const {
    return Config.ListenPort;
}

void MGatewayServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) {
    // 客户端连接注册（server→client 推送的目标解析）。服务器间连接（其他服务连
    // Gateway）也会先注册（首包是 MT_* 服务器协议，不会走 HandleClientPacket；
    // 广播下行发给它会被对端按非客户端包忽略，无害）。
    ClientConnections[ConnId] = Conn;
    MClientTargetResolver::Get().RegisterConn(Conn);
    LOG_INFO("Gateway skeleton accepted connection %llu", static_cast<unsigned long long>(ConnId));
    DispatchConnection(
        ConnId, Conn,
        [this, Conn](uint64 ConnectionId, const TByteArray& Payload) {
            if (Payload.empty()) {
                return;
            }
            const uint8 PacketType = Payload[0];
            // 服务器间入站：其他服务（如 EchoService）经 MServerConnection 调
            // Gateway 的 ServerCall（如 PushClientDownlink）。
            if (static_cast<EServerMessageType>(PacketType) == EServerMessageType::MT_FunctionCall) {
                TByteArray Data(Payload.begin() + 1, Payload.end());
                DispatchBackendServerCallPacketInbound(this, Conn, Data);
                return;
            }
            if (static_cast<EServerMessageType>(PacketType) == EServerMessageType::MT_FunctionResponse) {
                TByteArray Data(Payload.begin() + 1, Payload.end());
                HandleServerCallResponse(Data);
                return;
            }
            LOG_INFO("Gateway received client packet: conn=%llu size=%zu", static_cast<unsigned long long>(ConnectionId), Payload.size());
            HandleClientPacket(ConnectionId, Payload);
        },
        [this, Conn](uint64 ConnectionId) {
            ClientConnections.erase(ConnectionId);
            MClientTargetResolver::Get().UnregisterConn(Conn);
        });
}

void MGatewayServer::TickBackends() {
    MEndpointCache::Get().Tick(0.1f);
}

void MGatewayServer::ShutdownConnections() {
    for (auto& [ConnId, Conn] : ClientConnections) {
        (void)ConnId;
        if (Conn) {
            Conn->Close();
        }
    }
    ClientConnections.clear();
    MEndpointCache::Get().DeregisterAndShutdown();
}

void MGatewayServer::OnRunStarted() {
    LOG_INFO("Gateway skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}


MFuture<TResult<SEmptyServerMessage, FAppError>> MGatewayServer::PushClientDownlink(const FClientDownlinkPushRequest& Request) {
    if (Request.FunctionId == 0) {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>("client_downlink_function_required", "PushClientDownlink");
    }

    // connId==0 = 广播到全部在线客户端（与 RpcClientCall::CallClient(bBroadcast) 语义一致）。
    const bool bBroadcast = (Request.GatewayConnectionId == 0);

    TByteArray Packet;
    // step-2: 下行 envelope 走 BuildClientEnvelopePacket(无 MessageType 字节)。
    // RequestId = 0 表示这是服务器主动 push,UE 收到后 RequestId == 0 知道
    // 这是 push 而不是 response。Gateway 不分配 correlation id(step-3 task
    // 引入 CallClient 时如果需要再考虑)。
    if (!BuildClientEnvelopePacket(Request.FunctionId, /*RequestId=*/0, Request.Payload, Packet)) {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>("client_downlink_packet_build_failed", "PushClientDownlink");
    }

    if (bBroadcast) {
        uint32 Sent = 0;
        for (const auto& [ConnId, Conn] : ClientConnections) {
            if (Conn && Conn->IsConnected() && Conn->Send(Packet.data(), static_cast<uint32>(Packet.size()))) {
                ++Sent;
            }
        }
        if (Sent == 0) {
            return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>("gateway_client_connection_missing", "PushClientDownlink");
        }
        return MServerCallAsyncSupport::MakeSuccessFuture(SEmptyServerMessage{});
    }

    const auto It = ClientConnections.find(Request.GatewayConnectionId);
    if (It == ClientConnections.end() || !It->second || !It->second->IsConnected()) {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>("gateway_client_connection_missing", "PushClientDownlink");
    }

    if (!It->second->Send(Packet.data(), static_cast<uint32>(Packet.size()))) {
        return MServerCallAsyncSupport::MakeErrorFuture<SEmptyServerMessage>("client_downlink_send_failed", "PushClientDownlink");
    }

    return MServerCallAsyncSupport::MakeSuccessFuture(SEmptyServerMessage{});
}

MFuture<TResult<SEmptyServerMessage, FAppError>> MGatewayServer::Rpc_OnServerHandshake(uint32 /*DummyServerId*/) {
    return MServerCallAsyncSupport::MakeSuccessFuture(SEmptyServerMessage{});
}

MFuture<TResult<SEmptyServerMessage, FAppError>> MGatewayServer::Rpc_OnHeartbeat(uint32 /*Seq*/) {
    return MServerCallAsyncSupport::MakeSuccessFuture(SEmptyServerMessage{});
}

void MGatewayServer::HandleClientPacket(uint64 ConnectionId, const TByteArray& Data) {
    // step-2: Gateway 收到 envelope → 反射查 MClientManifest → 反射调对应
    // 绑定当前客户端连接为推送目标（MClientTargetContextGuard RAII——
    // 本次调用处理期间 CallClient 的下行解析到该连接）。
    const auto ClientIt = ClientConnections.find(ConnectionId);
    if (ClientIt != ClientConnections.end() && ClientIt->second) {
        MClientTargetContextGuard TargetGuard(ClientIt->second);
    }
    // MFUNCTION(ServerCall) → 反射回 envelope(无 MessageType 字节)。
    //
    // envelope 形态:[RequestId:8B][FunctionId:2B][PayloadSize:4B][Payload]
    uint16 FunctionId    = 0;
    uint64 RequestId     = 0;
    uint32 PayloadSize   = 0;
    size_t PayloadOffset = 0;
    if (!ParseClientEnvelopePacket(Data, FunctionId, RequestId, PayloadSize, PayloadOffset)) {
        LOG_WARN("Gateway client packet parse failed: connection=%llu", static_cast<unsigned long long>(ConnectionId));
        return;
    }

    TByteArray Payload;
    if (PayloadSize > 0) {
        Payload.insert(Payload.end(), Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset), Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    const MClientManifest::SEntry* Entry = MClientManifest::FindByFunctionId(FunctionId);

    // step-2: 不再有"PoC 退化硬编码 MEchoService"——manifest miss 直接 no-op
    // + LOG_WARN,等业务侧把 MFUNCTION(CallClient) 声明补上之后会自动走对。
    if (!Entry) {
        LOG_WARN("Gateway: unknown client function_id=%u (manifest miss) connection=%llu", static_cast<unsigned>(FunctionId), static_cast<unsigned long long>(ConnectionId));
        return;
    }

    // step-3: 转发到 EchoService（服务器间原始调用——Gateway 无 MEchoService
    // 反射，直接转发 FunctionId + 原始 Payload；Echo 收到后 DispatchBackendServerCallPacket
    // 按 FunctionId 分发到 Echo/EchoAwait，响应经 HandleServerCallResponse 回调回来）。
    auto SendClientError = [&](const char* Code) {
        TByteArray ErrorPacket;
        if (BuildClientEnvelopePacket(FunctionId, RequestId, TByteArray{}, ErrorPacket)) {
            const auto It = ClientConnections.find(ConnectionId);
            if (It != ClientConnections.end() && It->second) {
                It->second->Send(ErrorPacket.data(), static_cast<uint32>(ErrorPacket.size()));
            }
        }
        LOG_WARN("Gateway: forward failed code=%s function_id=%u", Code, static_cast<unsigned>(FunctionId));
    };

    TSharedPtr<MServerConnection> EchoConn = MEndpointCache::Get().GetOrConnect(EServerType::Echo);
    if (!EchoConn || !EchoConn->IsConnected()) {
        SendClientError("echo_connection_unavailable");
        return;
    }

    // 注册服务器调用（响应回调：把 Echo 的响应 Payload 原样回客户端 envelope）
    const uint64 CallId = RegisterServerCall(
        [this, ConnectionId, RequestId, FunctionId](const SServerCallResponse& Response) {
            TByteArray ClientPacket;
            if (BuildClientEnvelopePacket(FunctionId, RequestId, Response.Payload, ClientPacket)) {
                const auto It = ClientConnections.find(ConnectionId);
                if (It != ClientConnections.end() && It->second && It->second->IsConnected()) {
                    It->second->Send(ClientPacket.data(), static_cast<uint32>(ClientPacket.size()));
                }
            }
        },
        5.0, BuildServerCallLivenessProbe(EchoConn));

    if (CallId == 0) {
        SendClientError("server_call_register_failed");
        return;
    }

    TByteArray ServerPacket;
    const bool bSent = BuildServerCallPacket(FunctionId, CallId, Payload, ServerPacket) && SendServerCallMessage(EchoConn, ServerPacket);
    if (!bSent) {
        CancelServerCall(CallId);
        SendClientError("server_call_send_failed");
    } else {
    }
}

// CreateService 工厂——ServiceMain.h 中 namespace 内的
// CreateService<TService>() 模板已经用 NewMObject<TService>() 兜底
// 实例化。如果未来某 Service 想要更精细的初始化（例如注入外部依赖），
// 可以在此文件加特殊的 extern "C" 工厂并相应去掉兜底调用——目前不需要。