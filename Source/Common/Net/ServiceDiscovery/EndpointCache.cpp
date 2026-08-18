#include "Common/Net/ServiceDiscovery/EndpointCache.h"

#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Net/Rpc/RpcManifest.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/Rpc/RpcTransport.h"
#include "Common/Runtime/Actor/FActorMessage.h"
#include "Common/Runtime/Actor/MActorSystem.h"
#include "Common/Runtime/EventLoop/EventLoopGroup.h"
#include "Common/Runtime/Id.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Class.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Time.h"

#include <atomic>
#include <cstring>

namespace {
    // Forward decl: DispatchActorPostMessage 在 AttachDispatchToConnection 之后定义,
    // 但被其 switch case 引用—— 提前声明让编译器能找到。
    // MServerConnection 已在文件顶部通过 ServerConnection.h 完整 include。
    void DispatchActorPostMessage(MObject* Service, TSharedPtr<MServerConnection> Sender, const TByteArray& Data);

    uint64 NowMs() {
        return static_cast<uint64>(MTime::GetTimeSeconds() * 1000);
    }

    // 列出"需要主动拉 endpoint"的 EServerType —— 不含 Unknown。
    // 把硬编码的 {Gateway, Echo} 移到 ServerConnection.h 旁的 helper：新增业务
    // Service 类型只需在 EServerType 加一行 + 这里加一行，Cache 端不需改。
    TVector<EServerType> EnumerateBusinessServerTypes() {
        TVector<EServerType> Types;
        Types.push_back(EServerType::Gateway);
        Types.push_back(EServerType::Echo);
        // PoC 阶段只跑 Gateway + EchoService；Login/World/Scene/Router/Mgo
        // 历史 6 服合并为 SampleService 后不再单独注册。新业务类型按需追加。
        return Types;
    }

    // P2: 把每个 peer connection 的入站包分发到 DispatchBackendServerCallPacket
    // (incoming MT_FunctionCall) 或 HandleServerCallResponse (incoming MT_FunctionResponse).
    // 静态 helper(只在 EndpointCache.cpp 内,避免 MEndpointCache 公开 API 膨胀)。
    void AttachDispatchToConnection(TSharedPtr<MServerConnection> Conn, MObject* Service) {
        if (!Conn || !Service)
            return;
        Conn->SetOnMessage([Service](TSharedPtr<MServerConnection> Sender, uint8 PacketType, const TByteArray& Data) {
            switch (static_cast<EServerMessageType>(PacketType)) {
            case EServerMessageType::MT_FunctionCall:
                DispatchBackendServerCallPacket(Service, Sender, Data);
                break;
            case EServerMessageType::MT_FunctionResponse:
                HandleServerCallResponse(Data);
                break;
            case EServerMessageType::MT_ActorPost:
                // actor Post 远端路径:无 CallId,server 处理后**发投递状态 ack**
                // 绕过 DispatchBackendServerCallPacket 的 codegen 包装（避免 MPromise 分配 + 不会触发 SendServerCallResponse）
                // 改发 MT_ServerPush 通知 client 投递成功/失败（无 FunctionCall 回包,wasted RTT = 0）
                DispatchActorPostMessage(Service, Sender, Data);
                break;
            case EServerMessageType::MT_ServerPush:
                // MT_ServerPush: 服务端单向 push（投递状态/通知）。
                // 默认 handler 只 log;后续可挂 listener 做业务处理。
                if (!Data.empty()) {
                    LOG_DEBUG("MT_ServerPush: status=%u conn=%p",
                              static_cast<unsigned>(Data[0]), Sender.Get());
                }
                break;
            default:
                LOG_WARN("MEndpointCache: unknown packet type=%u on inbound", PacketType);
                break;
            }
        });
    }

    /**
     * @brief DispatchActorPostMessage - actor Post 远端的 server-side dispatch.
     *
     * 与 DispatchBackendServerCallPacket 的区别:
     * - 不分配 MPromise（无 future 链）
     * - **不发** MT_FunctionResponse（消除 wasted RTT）
     * - **发** MT_ServerPush 投递状态 ack（状态码 1 字节 + SequenceId 8B）
     * - 直接反射 invoke OnActorMessage 然后 return，actor dispatch 走 Sub 队列（async）
     *
     * wire format: [FunctionId:2B][Payload:N]
     *   FunctionId 必须在 receiver 端的反射表里存在（与 codegen 一致）
     *   Payload 是 FActorMessageWire 序列化后的字节
     *
     * reply format (MT_ServerPush): [StatusCode:1B][Reserved:2B][SequenceId:8B]
     *   StatusCode 0 = Delivered, 1 = ActorNotFound, 2 = ParseError, 3 = QueueFull
     *   SequenceId 大端,与 wire 中 Envelope.SequenceId 一致
     */
    void DispatchActorPostMessage(MObject* Service, TSharedPtr<MServerConnection> Sender, const TByteArray& Data) {
        if (!Service || Data.size() < 2) return;

        // 1) FunctionId:2B little-endian
        const uint16 FunctionId = static_cast<uint16>(Data[0]) | (static_cast<uint16>(Data[1]) << 8);

        // 2) find function by ID, 限定为 "OnActorMessage"（actor Post 入口）
        const MClass* Cls = Service->GetClass();
        if (!Cls) return;
        const MFunction* Function = Cls->FindFunctionById(FunctionId);
        if (!Function || Function->Name != "OnActorMessage") {
            LOG_WARN("DispatchActorPostMessage: unexpected function id=%u name=%s",
                     static_cast<unsigned>(FunctionId),
                     Function ? Function->Name.c_str() : "(none)");
            // 投递失败 → 仍然回 ServerPush 让 client 知道
            if (Sender && Sender->IsConnected()) {
                Sender->SendServerPush(1 /* ActorNotFound */);
            }
            return;
        }

        // 3) parse FActorMessageWire from payload
        FActorMessageWire Envelope;
        TResult<void, MString> ParseResult = ParsePayload(
            TByteArray(Data.begin() + 2, Data.end()), Envelope, "OnActorMessage");
        if (!ParseResult.IsOk()) {
            LOG_WARN("DispatchActorPostMessage: ParsePayload failed: %s", ParseResult.GetError().c_str());
            if (Sender && Sender->IsConnected()) {
                Sender->SendServerPush(2 /* ParseError */);
            }
            return;
        }

        // 4) build FActorMessage
        FActorMessage Msg;
        Msg.Header.SenderId    = Envelope.SenderId;
        Msg.Header.TargetId    = Envelope.TargetId;
        Msg.Header.MsgType     = Envelope.MsgType;
        Msg.Header.PayloadSize = static_cast<uint32_t>(Envelope.Payload.size());
        Msg.Payload            = Envelope.Payload;

        // 5) dispatch to local actor (Sub 线程 async, 不等 OnMessage 执行完)
        const bool bDispatched = MActorSystem::Get().DispatchLocalWithStatus(Envelope.TargetId, Msg);

        // 6) 发投递状态 ack —— MT_ServerPush + SequenceId（at-least-once 协议）
        // StatusCode: 0=Delivered, 3=QueueFull
        // Payload: SequenceId 8B big-endian（让 client 端能找到对应 outbox entry 删）
        if (Sender && Sender->IsConnected()) {
            const uint8 Status = bDispatched ? 0 /* Delivered */ : 3 /* QueueFull */;
            TByteArray AckPayload;
            AckPayload.resize(8);
            const uint64 Seq = Envelope.SequenceId;
            for (int i = 0; i < 8; ++i) {
                AckPayload[static_cast<size_t>(i)] = static_cast<uint8>((Seq >> (i * 8)) & 0xFFu);
            }
            Sender->SendServerPush(Status, std::move(AckPayload));
        }
    }
} // anonymous namespace

MEndpointCache& MEndpointCache::Get() {
    static MEndpointCache Instance;
    return Instance;
}

void MEndpointCache::AttachEventLoop(MNetEventLoop* InEventLoop) {
    std::lock_guard<std::mutex> Lock(Mutex);
    this->EventLoop = InEventLoop;
}

void MEndpointCache::SetServiceInstance(MObject* InServiceInstance) {
    std::lock_guard<std::mutex> Lock(Mutex);
    this->ServiceInstance = InServiceInstance;
    // Re-attach existing pooled connections that may have been created
    // before SetServiceInstance was called.
    for (auto& KV : ConnectionPool) {
        AttachDispatchToConnection(KV.second, InServiceInstance);
    }
}

MObject* MEndpointCache::GetServiceInstance() const {
    return ServiceInstance;
}

void MEndpointCache::BindRegistry(const MString& Addr, uint16 Port) {
    std::lock_guard<std::mutex> Lock(Mutex);
    Registry.Addr = Addr;
    Registry.Port = Port;
    TryConnectRegistry();
}

void MEndpointCache::RegisterLocal(const FServiceEndpoint& Self) {
    std::lock_guard<std::mutex> Lock(Mutex);
    Registry.LocalServerId   = Self.ServerId;
    Registry.LocalServerType = Self.ServerType;
    Registry.LocalActorIds   = Self.ActorIds;
    Registry.LocalAddress    = Self.Address;
    Registry.LocalPort       = Self.Port;
    if (!Registry.bConnected) {
        if (MLogIsDebugBuild()) {
            LOG_FATAL("MEndpointCache: registry not connected (DEBUG abort); addr=%s:%u", Registry.Addr.c_str(), static_cast<unsigned>(Registry.Port));
        }
        LOG_ERROR("MEndpointCache: registry not connected; addr=%s:%u — continuing degraded", Registry.Addr.c_str(), static_cast<unsigned>(Registry.Port));
        return;
    }
    TByteArray Packet;
    if (!RegistryProtocol::BuildRegistryRegisterPacket(Self, Packet)) {
        LOG_ERROR("MEndpointCache: build register packet failed");
        return;
    }
    SendToRegistry(Packet);
    // 主动拉一遍 Registry 已知的全部业务 Service 类型——避免硬编码 {Gateway, Echo}。
    // 注意：OnRegistryConnected 在连接建立时跑（此时 PendingListTypes 还是空的），
    // 首次启动的注册（RegisterLocal）在它之后——这里补发 ListEndpoints，否则会错过
    // 先注册服务（如 Echo）的 EndpointChange 推送（Registry 只推给当时已注册的会话）。
    for (EServerType T : EnumerateBusinessServerTypes()) {
        PendingListTypes.insert(T);
    }
    for (EServerType T : PendingListTypes) {
        TByteArray ListPacket;
        RegistryProtocol::BuildRegistryListEndpointsPacket(T, ListPacket);
        SendToRegistry(ListPacket);
    }
    PendingListTypes.clear();
}

TSharedPtr<MServerConnection> MEndpointCache::GetOrConnect(EServerType TargetServerType) {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto                        It = Endpoints.find(TargetServerType);
    if (It == Endpoints.end() || It->second.empty()) {
        if (MLogIsDebugBuild()) {
            LOG_WARN("MEndpointCache: no endpoints for type=%s", GetServerTypeDisplayName(TargetServerType));
        } else {
            LOG_DEBUG("MEndpointCache: no endpoints for type=%s", GetServerTypeDisplayName(TargetServerType));
        }
        return nullptr;
    }
    // round-robin：每次调用递增 index——放在 FRegistryClient 成员里，多进程
    // 共享单例时各 Service 独立累计起点，互不干扰。
    const uint32 Start = Registry.RoundRobinIdx++;
    for (size_t i = 0; i < It->second.size(); ++i) {
        const FServiceEndpoint& Ep = It->second[(Start + i) % It->second.size()];
        if (!Ep.bHealthy)
            continue;
        // 跳过本进程自己的端点——否则 round-robin 可能选中自己造成自连循环
        //（EchoA 转发 2001 时连回自己的 7001 → 再转发 → 连接不稳定）。
        if (Ep.ServerId == Registry.LocalServerId)
            continue;
        auto PoolIt = ConnectionPool.find(Ep.ServerId);
        if (PoolIt != ConnectionPool.end() && PoolIt->second && PoolIt->second->IsConnected()) {
            return PoolIt->second;
        }
        TSharedPtr<MServerConnection> Conn = LazyConnect(Ep);
        if (Conn) {
            return Conn;
        }
    }
    if (MLogIsDebugBuild()) {
        LOG_WARN("MEndpointCache: all endpoints unhealthy for type=%s", GetServerTypeDisplayName(TargetServerType));
    } else {
        LOG_DEBUG("MEndpointCache: all endpoints unhealthy for type=%s", GetServerTypeDisplayName(TargetServerType));
    }
    return nullptr;
}

void MEndpointCache::OnEndpointChange(EServerType ServerType, uint64 Seq, const TVector<FServiceEndpoint>& NewEndpoints) {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto                        SeqIt = LastSeq.find(ServerType);
    if (SeqIt != LastSeq.end() && Seq < SeqIt->second) {
        if (MLogIsDebugBuild()) {
            LOG_FATAL("MEndpointCache: stale push ServerType=%s Seq=%llu LastSeq=%llu", GetServerTypeDisplayName(ServerType), static_cast<unsigned long long>(Seq), static_cast<unsigned long long>(SeqIt->second));
        }
        LOG_WARN("MEndpointCache: stale push rejected ServerType=%s Seq=%llu LastSeq=%llu", GetServerTypeDisplayName(ServerType), static_cast<unsigned long long>(Seq), static_cast<unsigned long long>(SeqIt->second));
        return;
    }
    LastSeq[ServerType]   = Seq;
    Endpoints[ServerType] = NewEndpoints;

    TVector<uint32> Live;
    Live.reserve(NewEndpoints.size());
    for (const FServiceEndpoint& Ep : NewEndpoints) {
        // 把端点携带的 actor 注册到 MActorRouter——跨实例 actor 路由依赖它：
        // EchoA 收到 EchoB 的 EndpointChange（ActorIds 含 2001）后，FindActor(2001)
        // 才能命中（否则 SendToActor 表 miss → DefaultServer=Unknown → actor_route_invalid）。
        // 只注册"未存在"的 actor：本进程已注册的本地 actor（ServerType=Unknown，
        // IsActorLocal 判定依据）不能被端点信息覆盖成实际服务类型。
        for (uint64 ActorId : Ep.ActorIds) {
            if (MActorRouter::Get().FindActor(ActorId).ActorId == 0) {
                MActorRouter::Get().RegisterActor(ActorId, Ep.ServerType);
            }
            // === 阶段 C:actor 端点恢复时触发该 actor 的 outbox drain ===
            // outbox 里的消息是端点断开期间入队的,现在连接回来了,重发。
            MActorSystem::OnActorEndpointRecovered(ActorId, Ep.ServerType);
        }
        Live.push_back(Ep.ServerId);
    }
    PurgeDisappeared(Live);
}

void MEndpointCache::Tick(float DeltaTime) {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (!Registry.bConnected) {
        Registry.ReconnectTimer += DeltaTime;
        if (Registry.ReconnectTimer >= 5.0f) {
            Registry.ReconnectTimer = 0.0f;
            TryConnectRegistry();
        }
        return;
    }

    // 心跳发送（5s 节拍）——timer 累计放进 FRegistryClient 成员，多进程
    // 共享单例时各 Service 独立累计。
    Registry.HeartbeatTimer += DeltaTime;
    if (Registry.HeartbeatTimer >= 5.0f && Registry.LocalServerId != 0) {
        Registry.HeartbeatTimer = 0.0f;
        TByteArray Packet;
        RegistryProtocol::BuildRegistryHeartbeatPacket(Registry.LocalServerId, NowMs(), Packet);
        SendToRegistry(Packet);
    }

    // 出站业务连接（LazyConnect 建的 MServerConnection，如 Gateway→Echo、
    // Echo→Echo 的服务器间链路）收包驱动：它们没有 EventLoop 收包回调
    // （只有入站走 OnAccept→RegisterConnection），MServerConnection::Tick
    // 里的 ProcessRecv（Transport->ReceivePacket → HandlePacket → 服务器间
    // 响应分发）没有其他调用点——这里每帧驱动，否则对端响应永远收不到。
    for (auto& KV : ConnectionPool) {
        if (KV.second) {
            KV.second->Tick(DeltaTime);
        }
    }
}

void MEndpointCache::DeregisterAndShutdown() {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Registry.bConnected && Registry.LocalServerId != 0) {
        TByteArray Packet;
        RegistryProtocol::BuildRegistryDeregisterPacket(Registry.LocalServerId, Packet);
        SendToRegistry(Packet);
    }
    if (Registry.ConnId != 0 && EventLoop) {
        EventLoop->UnregisterConnection(Registry.ConnId);
    }
    Registry.ConnId = 0;
    if (Registry.Conn && Registry.Conn->IsConnected()) {
        Registry.Conn->Close();
    }
    Registry.bConnected = false;
    Registry.Conn.reset();
    ConnectionPool.clear();
}

void MEndpointCache::HandleRegistryPacket(const TByteArray& Packet) {
    if (Packet.empty())
        return;
    const uint8      PacketType = Packet[0];
    const TByteArray Payload(Packet.begin() + 1, Packet.end());
    switch (static_cast<EServiceRegistryMessageType>(PacketType)) {
    case EServiceRegistryMessageType::EndpointChange: {
        EServerType               Type = EServerType::Unknown;
        uint64                    Seq  = 0;
        TVector<FServiceEndpoint> Eps;
        if (!RegistryProtocol::ParseRegistryEndpointChangePacket(Payload, Type, Seq, Eps)) {
            LOG_WARN("MEndpointCache: malformed EndpointChange");
            return;
        }
        OnEndpointChange(Type, Seq, Eps);
        break;
    }
    case EServiceRegistryMessageType::Ack: {
        EServiceRegistryResult Status = EServiceRegistryResult::Ok;
        MString                Message;
        if (!RegistryProtocol::ParseRegistryAckPacket(Payload, Status, Message)) {
            LOG_WARN("MEndpointCache: malformed Ack");
            return;
        }
        if (Status != EServiceRegistryResult::Ok) {
            LOG_WARN("MEndpointCache: registry ack status=%u message=%s", static_cast<unsigned>(Status), Message.c_str());
        }
        break;
    }
    default:
        LOG_WARN("MEndpointCache: unexpected packet type=%u", static_cast<unsigned>(PacketType));
        break;
    }
}

void MEndpointCache::TryConnectRegistry() {
    if (Registry.Addr.empty() || Registry.Port == 0)
        return;
    if (Registry.bConnected)
        return;
    if (!EventLoop) {
        LOG_ERROR("MEndpointCache: EventLoop not attached; cannot connect registry");
        return;
    }
    if (!MSocket::EnsureInit()) {
        LOG_ERROR("MEndpointCache: socket init failed");
        return;
    }
    TSharedPtr<MTcpConnection> Tcp = MTcpConnection::ConnectTo(SSocketAddress(Registry.Addr, Registry.Port), 3.0f);
    if (!Tcp || !Tcp->IsConnected()) {
        if (MLogIsDebugBuild()) {
            LOG_FATAL("MEndpointCache: failed to connect registry at %s:%u (DEBUG abort)", Registry.Addr.c_str(), static_cast<unsigned>(Registry.Port));
        }
        LOG_ERROR("MEndpointCache: failed to connect registry at %s:%u — will retry in 5s", Registry.Addr.c_str(), static_cast<unsigned>(Registry.Port));
        return;
    }
    Registry.Conn       = Tcp;
    Registry.bConnected = true;
    // 把连接挂到 Service 进程内的 NetEventLoop 上轮询收包。
    // PacketType 1 字节 + payload 的 envelope 已经在 MTcpConnection::ReceivePacket
    // 那边按 length-prefixed 拆过；我们这里收一个完整 packet 后再按 1-byte type
    // 分发到 HandleRegistryPacket。
    Registry.ConnId     = MUniqueIdGenerator::Generate();
    const uint64 ConnId = Registry.ConnId;
    EventLoop->RegisterConnection(
        ConnId, Tcp, [this](uint64 /*Id*/, const TByteArray& Packet) { HandleRegistryPacket(Packet); },
        [this](uint64 Id) {
            std::lock_guard<std::mutex> Lock(Mutex);
            if (Id != Registry.ConnId)
                return;
            OnRegistryDisconnected();
        });
    OnRegistryConnected();
    LOG_INFO("MEndpointCache: connected to registry at %s:%u", Registry.Addr.c_str(), static_cast<unsigned>(Registry.Port));
}

void MEndpointCache::OnRegistryConnected() {
    // 如果已有 LocalInfo，重新 Register
    if (Registry.LocalServerId != 0) {
        FServiceEndpoint Self;
        Self.ServerId   = Registry.LocalServerId;
        Self.ServerType = Registry.LocalServerType;
        Self.Address    = Registry.LocalAddress;
        Self.Port       = Registry.LocalPort;
        Self.ActorIds   = Registry.LocalActorIds;
        TByteArray Packet;
        if (RegistryProtocol::BuildRegistryRegisterPacket(Self, Packet)) {
            SendToRegistry(Packet);
        }
    }
    for (EServerType T : PendingListTypes) {
        TByteArray Packet;
        RegistryProtocol::BuildRegistryListEndpointsPacket(T, Packet);
        SendToRegistry(Packet);
    }
    PendingListTypes.clear();
}

void MEndpointCache::OnRegistryDisconnected() {
    if (Registry.ConnId != 0 && EventLoop) {
        EventLoop->UnregisterConnection(Registry.ConnId);
    }
    Registry.ConnId = 0;
    Registry.Conn.reset();
    Registry.bConnected     = false;
    Registry.ReconnectTimer = 0.0f;
    LOG_WARN("MEndpointCache: registry disconnected");
}

void MEndpointCache::SendToRegistry(const TByteArray& Packet) {
    if (!Registry.bConnected || !Registry.Conn || !Registry.Conn->IsConnected())
        return;
    Registry.Conn->Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

TSharedPtr<MServerConnection> MEndpointCache::LazyConnect(const FServiceEndpoint& Ep) {
    // PoC 同主机：Address="0.0.0.0" 走 loopback。Service 端按 ServerType 解析
    // endpoint class name。
    const char* EndpointClass = GetServerEndpointClassName(Ep.ServerType);
    if (!EndpointClass) {
        LOG_WARN("MEndpointCache: no endpoint class for type=%s", GetServerTypeDisplayName(Ep.ServerType));
        return nullptr;
    }
    const uint32                  PeerServerId = MUniqueIdGenerator::Generate();
    SServerConnectionConfig       PeerConfig(PeerServerId, Ep.ServerType, EndpointClass,
                                             "127.0.0.1", // PoC: loopback regardless of registry Address field
                                             Ep.Port);
    TSharedPtr<MServerConnection> PeerConn = MakeShared<MServerConnection>(PeerConfig);
    if (!PeerConn->Connect()) {
        return nullptr;
    }
    ConnectionPool[Ep.ServerId] = PeerConn;
    // P2: wire inbound packet dispatch (MT_FunctionCall/Response).
    AttachDispatchToConnection(PeerConn, ServiceInstance);
    return PeerConn;
}

void MEndpointCache::PurgeDisappeared(const TVector<uint32>& LiveServerIds) {
    // 现有 pool entries 不在 LiveServerIds → 保留连接（不销毁）。
    // 注意：不能 erase——进行中的 CallServerFunction 的 LivenessProbe 持有
    // WeakConnection，销毁会让它在等待响应期间失效（server_call_disconnected）。
    // 连接是否真正断开由 MServerConnection::Tick（ProcessRecv 检测 EOF）自愈。
    // 仅清理已断开的连接（IsConnected false），避免池无限增长。
    for (auto It = ConnectionPool.begin(); It != ConnectionPool.end();) {
        bool bAlive = false;
        for (uint32 Id : LiveServerIds) {
            if (It->first == Id) {
                bAlive = true;
                break;
            }
        }
        if (!bAlive && (!It->second || !It->second->IsConnected())) {
            It = ConnectionPool.erase(It);
        } else {
            ++It;
        }
    }
}

// ---------------------------------------------------------------------------
// 多 Reactor 扩展(P5):per-Sub 重载. 锁粒度从全局 Mutex 降到各 Sub 自己的 Mutex.
// ---------------------------------------------------------------------------

void MEndpointCache::SetServiceInstance(uint32 SubId, MObject* ServiceInstance) {
    if (ServiceInstance == nullptr) {
        LOG_WARN("SetServiceInstance called with null instance for SubId=%u", static_cast<unsigned>(SubId));
        return;
    }

    // 1. 写入 PerSubService 映射(全局锁,只发生一次,启动期)
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        PerSubService[SubId] = ServiceInstance;
    }

    // 2. Re-attach 已经在这个 Sub 桶里的连接(防止 SetServiceInstance 之前
    //    已经创建了 connection 但还没 attach dispatch)
    //    SPerSubPool.Mutex 需要重新锁,所以不能持有全局 Mutex 时再去拿
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        auto                        It = PerSubPools.find(SubId);
        if (It != PerSubPools.end()) {
            SPerSubPool&                Pool = It->second;
            std::lock_guard<std::mutex> PoolLock(Pool.Mutex);
            for (auto& KV : Pool.Connections) {
                AttachDispatchToConnection(KV.second, ServiceInstance);
            }
        }
    }
}

MObject* MEndpointCache::GetServiceInstance(uint32 SubId) const {
    // 运行期只读,PerSubService 一旦注入不再变 —— 这里用 Mutex 是因为
    // std::map 不是 thread-safe,即使读也需要锁保护避免与 SetServiceInstance 写竞争.
    std::lock_guard<std::mutex> Lock(Mutex);
    auto                        It = PerSubService.find(SubId);
    if (It == PerSubService.end()) {
        return nullptr;
    }
    return It->second;
}

TSharedPtr<MServerConnection> MEndpointCache::GetOrConnect(EServerType TargetServerType, uint32 SubId) {
    // 1. 懒创建 per-Sub 池(全局锁,只发生一次,启动期+每个新 Sub 一次)
    //    用 try_emplace 而非 operator[] + std::move,因为 SPerSubPool 含
    //    std::mutex 不可移动/拷贝,operator[] 会触发 deleted operator= 错误
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        PerSubPools.try_emplace(SubId);
    }

    // 2. 拿 per-Sub 池引用,加 per-Sub 锁做选 endpoint + 复用/新建
    SPerSubPool* PoolPtr = nullptr;
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        PoolPtr = &PerSubPools[SubId];
    }

    std::lock_guard<std::mutex> PoolLock(PoolPtr->Mutex);

    // 3. 选 endpoint (与既有 GetOrConnect 同样逻辑,只是用 Endpoints 全局表)
    auto It = Endpoints.find(TargetServerType);
    if (It == Endpoints.end() || It->second.empty()) {
        if (MLogIsDebugBuild()) {
            LOG_WARN("MEndpointCache(per-Sub %u): no endpoints for type=%s", static_cast<unsigned>(SubId), GetServerTypeDisplayName(TargetServerType));
        } else {
            LOG_DEBUG("MEndpointCache(per-Sub %u): no endpoints for type=%s", static_cast<unsigned>(SubId), GetServerTypeDisplayName(TargetServerType));
        }
        return nullptr;
    }

    // 4. per-Sub 独立 round-robin (每 Sub 自己的起点,避免跨 Sub 偏差)
    const uint32 Start   = PoolPtr->RoundRobinIdx++;
    const uint64 LocalId = Registry.LocalServerId; // 读单例成员(不需锁,Register 后只写一次)
    for (size_t i = 0; i < It->second.size(); ++i) {
        const FServiceEndpoint& Ep = It->second[(Start + i) % It->second.size()];
        if (!Ep.bHealthy)
            continue;
        // 跳过本进程自连 —— 与既有 GetOrConnect 一致
        if (Ep.ServerId == LocalId)
            continue;

        // 5. 查 per-Sub 桶
        auto PoolIt = PoolPtr->Connections.find(Ep.ServerId);
        if (PoolIt != PoolPtr->Connections.end() && PoolIt->second && PoolIt->second->IsConnected()) {
            return PoolIt->second;
        }
        // 6. 复用既有 LazyConnect(共享 LazyConnect 内部逻辑,不复制)
        TSharedPtr<MServerConnection> Conn = LazyConnect(Ep);
        if (Conn) {
            PoolPtr->Connections[Ep.ServerId] = Conn;
            return Conn;
        }
    }

    if (MLogIsDebugBuild()) {
        LOG_WARN("MEndpointCache(per-Sub %u): all endpoints unhealthy for type=%s", static_cast<unsigned>(SubId), GetServerTypeDisplayName(TargetServerType));
    } else {
        LOG_DEBUG("MEndpointCache(per-Sub %u): all endpoints unhealthy for type=%s", static_cast<unsigned>(SubId), GetServerTypeDisplayName(TargetServerType));
    }
    return nullptr;
}
