/**
 * @file EndpointCache.cpp
 * @brief MEndpointCache 的实现:Registry 客户端连接、端点快照应用、服务器间连接池。
 *
 * @detail 本文件还内联了一段"服务器间入站包分发"逻辑(匿名 namespace 里的
 * AttachDispatchToConnection / DispatchActorPostMessage):
 * 出站连接没有 EventLoop 回调,入站包必须在这里按 MServerMessageType 自己分发,
 * 才会落到业务反射入口或 actor 投递 ack 路径上。
 */
#include "Common/Net/ServiceDiscovery/EndpointCache.h"

#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Net/Rpc/RpcManifest.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/Rpc/RpcTransport.h"
#include "Common/Net/ServerConnection.h"
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
    // 前向声明:DispatchActorPostMessage 定义在 AttachDispatchToConnection 之后,
    // 却被后者的 switch 分支引用 —— 先声明让编译器能找到。
    // MServerConnection 已在文件顶部通过 ServerConnection.h 完整 include。
    void DispatchActorPostMessage(MObject* Service, TSharedPtr<MServerConnection> Sender, const TByteArray& Data);

    // 当前时间戳(毫秒),用作心跳参数
    uint64 NowMs() {
        return static_cast<uint64>(MTime::GetTimeSeconds() * 1000);
    }

    /**
     * @brief EnumerateBusinessServerTypes - 列出"需要主动拉取端点"的业务类型。
     * @return 本轮要 ListEndpoints 的类型集合(不含 Unknown)
     *
     * @detail 现状只有 Gateway 与 Echo:新增业务类型时要同时在 EServerType 与这里各加一行
     * (Cache 的其他逻辑不用动)。注意 EServerType::Player 虽然已在枚举里、且
     * GetServerEndpointClassName 也为它配了 endpoint class,但**不在**本列表内 ——
     * 因此本进程不会主动拉 Player 的端点。
     */
    TVector<EServerType> EnumerateBusinessServerTypes() {
        TVector<EServerType> Types;
        Types.push_back(EServerType::Gateway);
        Types.push_back(EServerType::Echo);
        return Types;
    }

    /**
     * @brief AttachDispatchToConnection - 给出站连接挂上入站包分发回调。
     * @param Conn    目标连接(为空或未连接时什么都不做)
     * @param Service 业务反射入口(为空则直接返回;非拥有)
     *
     * @detail 只认三类包:MT_FunctionCall(走 codegen 的服务器间调用)、
     * MT_FunctionResponse(回到等待中的调用)、MT_ActorPost/MT_ServerPush(actor 投递路径)。
     * 其余类型记 WARN 丢弃。这个 lambda 捕获裸指针 Service,调用方必须保证其生命周期覆盖连接。
     */
    void AttachDispatchToConnection(TSharedPtr<MServerConnection> Conn, MObject* Service) {
        if (!Conn || !Service)
            return;
        Conn->SetOnMessage([Service](TSharedPtr<MServerConnection> Sender, uint8 PacketType, const TByteArray& Data) {
            switch (static_cast<EServerMessageType>(PacketType)) {
            case EServerMessageType::MT_FunctionCall:
                DispatchBackendServerCallPacket(Service, Sender, Data);
                break;
            case EServerMessageType::MT_FunctionResponse:
                HandleServerCallResponse(Data, Sender.get());
                break;
            case EServerMessageType::MT_ActorPost:
                // actor Post 的远端入口:没有 CallId,server 处理完**发投递状态 ack**,
                // 绕过 DispatchBackendServerCallPacket 的 codegen 包装(省掉 MPromise 分配,
                // 也不会触发 SendServerCallResponse),改发 MT_ServerPush 通知发起端投递结果
                // —— 没有 FunctionCall 回包,wasted RTT = 0
                DispatchActorPostMessage(Service, Sender, Data);
                break;
            case EServerMessageType::MT_ServerPush:
                // 投递 ack 的解析:线上布局由生产端唯一确定 ——
                //   MServerConnection::SendServerPush 先写 [StatusCode:1B][Reserved:2B] 前缀,
                //   再拼上调用方给的 payload;而唯一的 payload 生产者(下方
                //   DispatchActorPostMessage)造的 payload 自身已含 1 字节 StatusCode,即
                //   [StatusCode:1B][ActorId:8B][SequenceId:8B]。于是线上整包是
                //   [StatusCode][Reserved:2B][StatusCode][ActorId:8B][SequenceId:8B](20 字节),
                //   首字节与 payload 内那个 StatusCode 恒为同一个值,因此 Status 取 Data[0],
                //   ActorId / SequenceId 则要从第 4 字节起算。
                // 解析出 ActorId/SequenceId 后调 MActorSystem::AckOutboxByRoute 删掉对应
                // route 的 outbox entry(fan-out 场景只删这一条,其他 route 还在等 ack),
                // 最后 fire 所有已注册的 listener(业务订阅可观测性)。
                if (Data.size() >= 3 + 1 + 8 + 8) {
                    const uint8 Status  = Data[0];
                    uint64      ActorId = 0, SequenceId = 0;
                    // 跳过 [StatusCode:1B] + [Reserved:2B] + payload 内的 [StatusCode:1B]
                    for (int i = 0; i < 8; ++i) {
                        ActorId |= static_cast<uint64>(Data[4 + i]) << (i * 8);
                        SequenceId |= static_cast<uint64>(Data[12 + i]) << (i * 8);
                    }
                    if (Status == 0 /* Delivered:已投递 */ && Sender) {
                        // 1:N fan-out:按连接对应的 ServerType 精确删除该 route 的 outbox entry
                        const EServerType Route = Sender->GetConfig().ServerType;
                        MActorSystem::Get().AckOutboxByRoute(ActorId, SequenceId, Route);
                    } else if (Status != 0) {
                        // 否定 ack:没有可重投路径,只留调试日志
                        LOG_DEBUG("MT_ServerPush: negative ack status=%u actor=%llu seq=%llu", static_cast<unsigned>(Status), static_cast<unsigned long long>(ActorId), static_cast<unsigned long long>(SequenceId));
                    }
                    // fire 业务 listener(投递状态可观测 / 业务自定义 server push 通知)
                    MActorSystem::Get().FireServerPushListeners(Status, ActorId, SequenceId);
                } else {
                    LOG_DEBUG("MT_ServerPush: short payload (%zu bytes), ignoring", Data.size());
                }
                break;
            default:
                LOG_WARN("MEndpointCache: unknown packet type=%u on inbound", PacketType);
                break;
            }
        });
    }

    /**
     * @brief DispatchActorPostMessage - actor Post 远端包在服务端的落地处理。
     * @param Service 业务反射入口(为空即返回)
     * @param Sender  发起方连接;用它回投递状态 ack(可能为空或已断开)
     * @param Data    包体:[FunctionId:2B][FActorMessageWire 序列化字节]
     *
     * @detail 与 DispatchBackendServerCallPacket 的差别:
     *   - 不分配 MPromise(没有 future 链);
     *   - **不发** MT_FunctionResponse(消除 wasted RTT);
     *   - **发** MT_ServerPush 投递状态 ack;
     *   - 反射 invoke OnActorMessage 后立即返回,actor 侧分发走 Sub 队列(异步,不等执行完)。
     *
     * 入站格式:[FunctionId:2B 小端][Payload:N]
     *   FunctionId 必须在接收端反射表里,且解析出来必须是 "OnActorMessage"(actor Post 入口)。
     * 回包格式(本函数构造的 payload):[StatusCode:1B][ActorId:8B][SequenceId:8B] 小端
     *   StatusCode:0 = Delivered, 1 = ActorNotFound, 2 = ParseError, 3 = QueueFull
     *   ActorId 取 Envelope.TargetId(让发起端知道该删哪个 actor 的 outbox entry);
     *   SequenceId 取 Envelope.SequenceId(让发起端在 outbox 里定位并删除对应 entry)。
     *   注意:该 payload 还会被 MServerConnection::SendServerPush 在前面再补一层
     *   [StatusCode:1B][Reserved:2B],因此线上整包是
     *   [StatusCode:1B][Reserved:2B][StatusCode:1B][ActorId:8B][SequenceId:8B](共 20 字节),
     *   其中前缀里的 StatusCode 与本 payload 内那个 StatusCode 恒为同一个值。
     *   接收端解析(见本文件 MT_ServerPush 分支)必须按这个 20 字节布局算偏移。
     */
    void DispatchActorPostMessage(MObject* Service, TSharedPtr<MServerConnection> Sender, const TByteArray& Data) {
        // 至少要能读出 2 字节 FunctionId
        if (!Service || Data.size() < 2)
            return;

        // 1) FunctionId:2 字节小端
        const uint16 FunctionId = static_cast<uint16>(Data[0]) | (static_cast<uint16>(Data[1]) << 8);

        // 2) 按 ID 找反射函数,并限定必须是 "OnActorMessage"(actor Post 的入口函数名)
        const MClass* Cls = Service->GetClass();
        if (!Cls)
            return;
        const MFunction* Function = Cls->FindFunctionById(FunctionId);
        if (!Function || Function->Name != "OnActorMessage") {
            LOG_WARN("DispatchActorPostMessage: unexpected function id=%u name=%s", static_cast<unsigned>(FunctionId), Function ? Function->Name.c_str() : "(none)");
            // 投递失败也要回 ack,让发起端能及时清 outbox
            if (Sender && Sender->IsConnected()) {
                TByteArray ErrAck(1 + 8 + 8, 0);
                ErrAck[0] = 1; // ActorNotFound:目标 actor 不在本进程
                Sender->SendServerPush(1, std::move(ErrAck));
            }
            return;
        }

        // 3) 从 payload 解出 FActorMessageWire 信封(跳过 2 字节 FunctionId)
        FActorMessageWire      Envelope;
        TResult<void, MString> ParseResult = ParsePayload(TByteArray(Data.begin() + 2, Data.end()), Envelope, "OnActorMessage");
        if (!ParseResult.IsOk()) {
            LOG_WARN("DispatchActorPostMessage: ParsePayload failed: %s", ParseResult.GetError().c_str());
            if (Sender && Sender->IsConnected()) {
                TByteArray ErrAck(1 + 8 + 8, 0);
                ErrAck[0] = 2; // ParseError:包体解析失败
                Sender->SendServerPush(2, std::move(ErrAck));
            }
            return;
        }

        // 4) 组装本地投递用的 FActorMessage
        FActorMessage Msg;
        Msg.Header.SenderId    = Envelope.SenderId;
        Msg.Header.TargetId    = Envelope.TargetId;
        Msg.Header.MsgType     = Envelope.MsgType;
        Msg.Header.PayloadSize = static_cast<uint32_t>(Envelope.Payload.size());
        Msg.Payload            = Envelope.Payload;

        // 5) 投递给本地 actor(Sub 线程异步,不等 OnMessage 执行完)
        const bool bDispatched = MActorSystem::Get().DispatchLocalWithStatus(Envelope.TargetId, Msg);

        // 6) 回投递状态 ack —— MT_ServerPush + (ActorId, SequenceId) 小端
        // 让发起端用 AckOutbox(ActorId, Seq) 找到并删除对应 outbox entry
        if (Sender && Sender->IsConnected()) {
            const uint8 Status = bDispatched ? 0 /* Delivered:已投递 */ : 3 /* QueueFull:actor 队列已满 */;
            TByteArray  AckPayload;
            AckPayload.resize(1 + 8 + 8, 0);
            AckPayload[0]             = Status;
            const uint64 ActorIdLE    = Envelope.TargetId;
            const uint64 SequenceIdLE = Envelope.SequenceId;
            // 手工按小端铺字节:wire 格式不依赖主机字节序
            for (int i = 0; i < 8; ++i) {
                AckPayload[1 + i]     = static_cast<uint8>((ActorIdLE >> (i * 8)) & 0xFFu);
                AckPayload[1 + 8 + i] = static_cast<uint8>((SequenceIdLE >> (i * 8)) & 0xFFu);
            }
            Sender->SendServerPush(Status, std::move(AckPayload));
        }
    }
} // anonymous namespace

MEndpointCache& MEndpointCache::Get() {
    // 进程级单例:C++11 起局部 static 的初始化线程安全,首次调用时构造
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
    // 重新挂一遍已有池内连接的回调:补偿"连接先于 SetServiceInstance 建立"的顺序
    for (auto& KV : ConnectionPool) {
        AttachDispatchToConnection(KV.second, InServiceInstance);
    }
}

MObject* MEndpointCache::GetServiceInstance() const {
    // 只读一个裸指针,不取锁(调用点都在初始化完成之后)
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
        // 只有日志级别的差别:LOG_FATAL 是 Critical 级异步日志,并不会 abort
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
    // 主动拉一遍全部业务类型,避免依赖硬编码的 {Gateway, Echo}。
    // 必须在这里补发的原因:OnRegistryConnected 是连接建立时跑的,那会儿
    // PendingListTypes 还是空的;首次启动的注册发生在其后,不补发就会错过
    // 先注册服务(如 Echo)的 EndpointChange 推送 —— Registry 只推给当时已注册的会话。
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

void MEndpointCache::UpdateLocalActorIds(const TVector<uint64>& InActorIds) {
    std::lock_guard<std::mutex> Lock(Mutex);
    Registry.LocalActorIds = InActorIds;
    // 没连上或还没注册成功时只留本地副本:包发出去也会因未知 ServerId 被拒
    if (!Registry.bConnected || Registry.LocalServerId == 0) {
        return;
    }
    TByteArray Packet;
    if (!RegistryProtocol::BuildRegistryUpdateActorsPacket(Registry.LocalServerId, InActorIds, Packet)) {
        LOG_ERROR("MEndpointCache: build update-actors packet failed");
        return;
    }
    SendToRegistry(Packet);
}

TSharedPtr<MServerConnection> MEndpointCache::GetOrConnect(EServerType TargetServerType) {
    FServiceEndpoint Selected;
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        auto                        It = Endpoints.find(TargetServerType);
        if (It == Endpoints.end() || It->second.empty()) {
        // 调试构建下用 WARN 提高可见性,发布构建降级为 DEBUG 避免刷日志
        if (MLogIsDebugBuild()) {
            LOG_WARN("MEndpointCache: no endpoints for type=%s", GetServerTypeDisplayName(TargetServerType));
        } else {
            LOG_DEBUG("MEndpointCache: no endpoints for type=%s", GetServerTypeDisplayName(TargetServerType));
        }
            return nullptr;
        }
    // round-robin:起点放在 FRegistryClient 成员里,每个进程各自累计(各 Service 互不干扰)
        const uint32 Start = Registry.RoundRobinIdx++;
        for (size_t i = 0; i < It->second.size(); ++i) {
        const FServiceEndpoint& Ep = It->second[(Start + i) % It->second.size()];
        if (!Ep.bHealthy)
            continue;
        // 跳过本进程自己的端点:否则 round-robin 可能选中自己形成自连环
        // (A 进程转发时连回自己的监听端口 → 再转发 → 连接反复重建,表现为不稳定)
        if (Ep.ServerId == Registry.LocalServerId)
            continue;
            auto PoolIt = ConnectionPool.find(Ep.ServerId);
        if (PoolIt != ConnectionPool.end() && PoolIt->second && PoolIt->second->IsConnected()) {
                return PoolIt->second;
            }
            Selected = Ep;
            break;
        }
    }
    if (Selected.ServerId != 0) {
        // Connecting may block and invokes callbacks; never do it while holding Cache::Mutex.
        TSharedPtr<MServerConnection> Conn = LazyConnect(Selected);
        if (!Conn) return nullptr;
        std::lock_guard<std::mutex> Lock(Mutex);
        auto It = ConnectionPool.find(Selected.ServerId);
        if (It != ConnectionPool.end() && It->second && It->second->IsConnected()) return It->second;
        ConnectionPool[Selected.ServerId] = Conn;
        return Conn;
    }
    if (MLogIsDebugBuild()) {
        LOG_WARN("MEndpointCache: all endpoints unhealthy for type=%s", GetServerTypeDisplayName(TargetServerType));
    } else {
        LOG_DEBUG("MEndpointCache: all endpoints unhealthy for type=%s", GetServerTypeDisplayName(TargetServerType));
    }
    return nullptr;
}

void MEndpointCache::OnEndpointChange(EServerType ServerType, uint64 Seq, const TVector<FServiceEndpoint>& NewEndpoints) {
    {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto                        SeqIt = LastSeq.find(ServerType);
    // 只接受"不早于已记录序号"的推送:乱序到达的旧快照直接丢弃(否则会用旧数据覆盖新数据)
    if (SeqIt != LastSeq.end() && Seq < SeqIt->second) {
        if (MLogIsDebugBuild()) {
            LOG_FATAL("MEndpointCache: stale push ServerType=%s Seq=%llu LastSeq=%llu", GetServerTypeDisplayName(ServerType), static_cast<unsigned long long>(Seq), static_cast<unsigned long long>(SeqIt->second));
        }
        LOG_WARN("MEndpointCache: stale push rejected ServerType=%s Seq=%llu LastSeq=%llu", GetServerTypeDisplayName(ServerType), static_cast<unsigned long long>(Seq), static_cast<unsigned long long>(SeqIt->second));
        return;
    }
    LastSeq[ServerType]   = Seq;
    Endpoints[ServerType] = NewEndpoints; // 全量替换语义
    }

    TVector<uint32> Live;
    Live.reserve(NewEndpoints.size());
    for (const FServiceEndpoint& Ep : NewEndpoints) {
        // 把端点携带的 actor 登记到 MActorRouter —— 跨实例 actor 路由依赖它:
        // 收到对端的 EndpointChange 后 FindActor 才能命中,否则 SendToActor 表 miss
        // 会走到 actor_route_invalid。只登记"尚不存在"的 actor:本进程已注册的本地 actor
        // 不能被远端端点信息覆盖掉归属(本地 actor 以 Unknown 作为判定依据)。
        for (uint64 ActorId : Ep.ActorIds) {
            if (MActorRouter::Get().FindActor(ActorId).ActorId == 0) {
                MActorRouter::Get().RegisterActor(ActorId, Ep.ServerType);
            }
            // 阶段 C:端点恢复(重连/重新注册)时触发该 actor 的 outbox drain ——
            // 断开期间入队的消息在这里被重发
            MActorSystem::OnActorEndpointRecovered(ActorId, Ep.ServerType);
        }
        Live.push_back(Ep.ServerId);
    }
    PurgeDisappeared(Live);
}

void MEndpointCache::Tick(float DeltaTime) {
    TVector<TSharedPtr<MServerConnection>> Connections;
    bool bReconnect = false;
    {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (!Registry.bConnected) {
        // 断线状态:累计到 5s 才重连一次,避免连接建立失败时无限打点
        Registry.ReconnectTimer += DeltaTime;
        if (Registry.ReconnectTimer >= 5.0f) {
            Registry.ReconnectTimer = 0.0f;
            bReconnect = true;
        }
    } else {
        // 心跳发送(5s 节拍):计时器放 FRegistryClient 成员里,每个进程独立累计
        Registry.HeartbeatTimer += DeltaTime;
        if (Registry.HeartbeatTimer >= 5.0f && Registry.LocalServerId != 0) {
            Registry.HeartbeatTimer = 0.0f;
            TByteArray Packet;
            RegistryProtocol::BuildRegistryHeartbeatPacket(Registry.LocalServerId, NowMs(), Packet);
            SendToRegistry(Packet);
        }

        // Snapshot the pool under the lock, then drive connections after releasing it.
        for (auto& KV : ConnectionPool) if (KV.second) Connections.push_back(KV.second);
    }
    }
    if (bReconnect) {
        TryConnectRegistry();
        return;
    }
    for (const auto& Connection : Connections) Connection->Tick(DeltaTime);
}

void MEndpointCache::DeregisterAndShutdown() {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Registry.bConnected && Registry.LocalServerId != 0) {
        // 尽力而为:发送失败也不阻塞退出
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
    // 直接丢弃整池连接:MServerConnection 析构会 Disconnect
    ConnectionPool.clear();
}

void MEndpointCache::HandleRegistryPacket(const TByteArray& Packet) {
    if (Packet.empty())
        return;
    // 第 1 字节是类型,其余是 payload(长度前缀已在 MTcpConnection 层剥掉)
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
        // 失败 ack 只记日志:注册失败会在 Tick 的心跳/重连节奏里自然重试
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
    // 前置条件不全时静默返回:由调用方(BindRegistry / Tick)在条件具备后重新触发
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
    // 同步连接,超时 3s —— 因此本函数不能在每帧路径上无节制调用(有 5s 节流兜着)
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
    // 挂到 Service 进程内的 NetEventLoop 上收包:长度前缀已由
    // MTcpConnection::ReceivePacket 剥掉,这里拿到的是一个完整包,再按 1 字节类型分发。
    Registry.ConnId     = MUniqueIdGenerator::Generate();
    const uint64 ConnId = Registry.ConnId;
    EventLoop->RegisterConnection(
        ConnId, Tcp, [this](uint64 /*Id*/, const TByteArray& Packet) { HandleRegistryPacket(Packet); },
        [this](uint64 Id) {
            // 断线回调可能由别处触发,先确认仍然是自己这条连接再清理
            std::lock_guard<std::mutex> Lock(Mutex);
            if (Id != Registry.ConnId)
                return;
            OnRegistryDisconnected();
        });
    OnRegistryConnected();
    LOG_INFO("MEndpointCache: connected to registry at %s:%u", Registry.Addr.c_str(), static_cast<unsigned>(Registry.Port));
}

void MEndpointCache::OnRegistryConnected() {
    // 已有本地信息(重连场景)就重新注册一次
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
    // 补发此前挂起的 ListEndpoints 请求
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
    Registry.ReconnectTimer = 0.0f; // 从下一次 Tick 重新开始 5s 计时
    LOG_WARN("MEndpointCache: registry disconnected");
}

void MEndpointCache::SendToRegistry(const TByteArray& Packet) {
    // 未连接就静默丢弃:本层不做离线排队(注册/心跳都靠定时重试)
    if (!Registry.bConnected || !Registry.Conn || !Registry.Conn->IsConnected())
        return;
    Registry.Conn->Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

TSharedPtr<MServerConnection> MEndpointCache::LazyConnect(const FServiceEndpoint& Ep) {
    // PoC 同主机:Address="0.0.0.0" 时统一走 loopback;按 ServerType 解析对端 endpoint class 名
    const char* EndpointClass = GetServerEndpointClassName(Ep.ServerType);
    if (!EndpointClass) {
        LOG_WARN("MEndpointCache: no endpoint class for type=%s", GetServerTypeDisplayName(Ep.ServerType));
        return nullptr;
    }
    const uint32                  PeerServerId = MUniqueIdGenerator::Generate();
    SServerConnectionConfig       PeerConfig(PeerServerId, Ep.ServerType, EndpointClass,
                                             "127.0.0.1", // PoC:忽略 registry 里的 Address,一律 loopback
                                             Ep.Port);
    TSharedPtr<MServerConnection> PeerConn = MakeShared<MServerConnection>(PeerConfig);
    if (!PeerConn->Connect()) {
        return nullptr;
    }
    // P2:挂上入站包分发(MT_FunctionCall / MT_FunctionResponse)
    AttachDispatchToConnection(PeerConn, ServiceInstance);
    return PeerConn;
}

void MEndpointCache::PurgeDisappeared(const TVector<uint32>& LiveServerIds) {
    std::lock_guard<std::mutex> Lock(Mutex);
    // 只清理"已消失 **且** 未连接"的池条目:不能单凭快照把它 erase 掉 ——
    // 进行中的 CallServerFunction 的活跃性探测持有 WeakConnection,
    // 提前销毁会让它在等待响应期间直接失效(表现为 server_call_disconnected)。
    // 连接是否真的断了由 MServerConnection::Tick(ProcessRecv 收到 EOF)自愈。
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
// 多 Reactor 扩展(P5):per-Sub 重载。锁粒度从全局 Mutex 降到各 Sub 自己的 Mutex。
// ---------------------------------------------------------------------------

void MEndpointCache::SetServiceInstance(uint32 SubId, MObject* ServiceInstance) {
    if (ServiceInstance == nullptr) {
        LOG_WARN("SetServiceInstance called with null instance for SubId=%u", static_cast<unsigned>(SubId));
        return;
    }

    // 1. 写入 PerSubService 映射(全局锁;只在启动期发生一次)
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        PerSubService[SubId] = ServiceInstance;
    }

    // 2. 重新 attach 这个 Sub 桶里已有的连接(补偿"连接先建、实例后装")
    //    这里需要再拿 SPerSubPool::Mutex,所以不能与全局 Mutex 同时持有
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        auto It = PerSubPools.find(SubId);
        if (It != PerSubPools.end() && It->second) {
            TSharedPtr<SPerSubPool> Pool = It->second;
            std::lock_guard<std::mutex> PoolLock(Pool->Mutex);
            for (auto& KV : Pool->Connections) {
                AttachDispatchToConnection(KV.second, ServiceInstance);
            }
        }
    }
}

MObject* MEndpointCache::GetServiceInstance(uint32 SubId) const {
    // 运行期只读:PerSubService 一旦注入不再变。加锁是因为 TMap 本身非线程安全,
    // 即使只读也要与 SetServiceInstance 的写入互斥。
    std::lock_guard<std::mutex> Lock(Mutex);
    auto                        It = PerSubService.find(SubId);
    if (It == PerSubService.end()) {
        return nullptr;
    }
    return It->second;
}

TSharedPtr<MServerConnection> MEndpointCache::GetOrConnect(EServerType TargetServerType, uint32 SubId) {
    // 1. 懒创建 per-Sub 池(全局锁;每个 Sub 只发生一次)
    //    用 try_emplace 而不是 operator[] + move:SPerSubPool 含 std::mutex,
    //    不可移动/拷贝,operator[] 会触发 deleted operator=(典型多 Reactor 写法陷阱)
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        auto [It, Inserted] = PerSubPools.try_emplace(SubId, MakeShared<SPerSubPool>());
        (void)It;
        (void)Inserted;
    }

    // 2. 复制端点快照与稳定的 per-Sub 池引用。后续不能持全局锁执行建连，
    // 因为 Connect / callback 可能重入 EndpointCache。
    TSharedPtr<SPerSubPool> PoolPtr;
    TVector<FServiceEndpoint> EndpointSnapshot;
    uint64 LocalId = 0;
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        PoolPtr = PerSubPools[SubId];
        auto It = Endpoints.find(TargetServerType);
        if (It != Endpoints.end()) {
            EndpointSnapshot = It->second;
        }
        LocalId = Registry.LocalServerId;
    }

    if (!PoolPtr || EndpointSnapshot.empty()) {
        if (MLogIsDebugBuild()) {
            LOG_WARN("MEndpointCache(per-Sub %u): no endpoints for type=%s", static_cast<unsigned>(SubId), GetServerTypeDisplayName(TargetServerType));
        } else {
            LOG_DEBUG("MEndpointCache(per-Sub %u): no endpoints for type=%s", static_cast<unsigned>(SubId), GetServerTypeDisplayName(TargetServerType));
        }
        return nullptr;
    }

    std::lock_guard<std::mutex> PoolLock(PoolPtr->Mutex);

    // 3. per-Sub 独立的 round-robin 起点,避免各 Sub 长期打同一个端点
    const uint32 Start   = PoolPtr->RoundRobinIdx++;
    for (size_t i = 0; i < EndpointSnapshot.size(); ++i) {
        const FServiceEndpoint& Ep = EndpointSnapshot[(Start + i) % EndpointSnapshot.size()];
        if (!Ep.bHealthy)
            continue;
        // 跳过本进程自己 —— 与进程级版本一致,避免自连环
        if (Ep.ServerId == LocalId)
            continue;

        // 5. 先查 per-Sub 桶
        auto PoolIt = PoolPtr->Connections.find(Ep.ServerId);
        if (PoolIt != PoolPtr->Connections.end() && PoolIt->second && PoolIt->second->IsConnected()) {
            return PoolIt->second;
        }
        // 6. 复用既有 LazyConnect(注意:它仍会把连接写进全局 ConnectionPool,两处都留索引)
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
