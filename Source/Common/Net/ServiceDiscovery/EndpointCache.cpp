#include "Common/Net/ServiceDiscovery/EndpointCache.h"

#include "Common/Net/Rpc/RpcManifest.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/Rpc/RpcTransport.h"
#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Runtime/EventLoop/EventLoop.h"
#include "Common/Runtime/Id.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Time.h"

#include <atomic>
#include <cstring>

namespace
{
uint64 NowMs()
{
    return static_cast<uint64>(MTime::GetTimeSeconds() * 1000);
}

// 列出"需要主动拉 endpoint"的 EServerType —— 不含 Unknown。
// 把硬编码的 {Gateway, Echo} 移到 ServerConnection.h 旁的 helper：新增业务
// Service 类型只需在 EServerType 加一行 + 这里加一行，Cache 端不需改。
TVector<EServerType> EnumerateBusinessServerTypes()
{
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
void AttachDispatchToConnection(TSharedPtr<MServerConnection> Conn, MObject* Service)
{
    if (!Conn || !Service) return;
    Conn->SetOnMessage(
        [Service](TSharedPtr<MServerConnection> Sender, uint8 PacketType, const TByteArray& Data)
        {
            switch (static_cast<EServerMessageType>(PacketType))
            {
                case EServerMessageType::MT_FunctionCall:
                    DispatchBackendServerCallPacket(Service, Sender, Data);
                    break;
                case EServerMessageType::MT_FunctionResponse:
                    HandleServerCallResponse(Data);
                    break;
                default:
                    LOG_WARN("MEndpointCache: unknown packet type=%u on inbound", PacketType);
                    break;
            }
        });
}
} // anonymous namespace

MEndpointCache& MEndpointCache::Get()
{
    static MEndpointCache Instance;
    return Instance;
}

void MEndpointCache::AttachEventLoop(MNetEventLoop* EventLoop)
{
    std::lock_guard<std::mutex> Lock(Mutex_);
    EventLoop_ = EventLoop;
}

void MEndpointCache::SetServiceInstance(MObject* ServiceInstance)
{
    std::lock_guard<std::mutex> Lock(Mutex_);
    ServiceInstance_ = ServiceInstance;
    // Re-attach existing pooled connections that may have been created
    // before SetServiceInstance was called.
    for (auto& KV : ConnectionPool_)
    {
        AttachDispatchToConnection(KV.second, ServiceInstance);
    }
}

MObject* MEndpointCache::GetServiceInstance() const
{
    return ServiceInstance_;
}

void MEndpointCache::BindRegistry(const MString& Addr, uint16 Port)
{
    std::lock_guard<std::mutex> Lock(Mutex_);
    Registry_.Addr = Addr;
    Registry_.Port = Port;
    TryConnectRegistry();
}

void MEndpointCache::RegisterLocal(const FServiceEndpoint& Self)
{
    std::lock_guard<std::mutex> Lock(Mutex_);
    Registry_.LocalServerId = Self.ServerId;
    Registry_.LocalServerType = Self.ServerType;
    Registry_.LocalActorIds = Self.ActorIds;
    Registry_.LocalAddress = Self.Address;
    Registry_.LocalPort = Self.Port;
    if (!Registry_.bConnected)
    {
        if (MLogIsDebugBuild())
        {
            LOG_FATAL("MEndpointCache: registry not connected (DEBUG abort); addr=%s:%u",
                      Registry_.Addr.c_str(), static_cast<unsigned>(Registry_.Port));
        }
        LOG_ERROR("MEndpointCache: registry not connected; addr=%s:%u — continuing degraded",
                  Registry_.Addr.c_str(), static_cast<unsigned>(Registry_.Port));
        return;
    }
    TByteArray Packet;
    if (!RegistryProtocol::BuildRegistryRegisterPacket(Self, Packet))
    {
        LOG_ERROR("MEndpointCache: build register packet failed");
        return;
    }
    SendToRegistry(Packet);
    // 主动拉一遍 Registry 已知的全部业务 Service 类型——避免硬编码 {Gateway, Echo}。
    // 注意：OnRegistryConnected 在连接建立时跑（此时 PendingListTypes_ 还是空的），
    // 首次启动的注册（RegisterLocal）在它之后——这里补发 ListEndpoints，否则会错过
    // 先注册服务（如 Echo）的 EndpointChange 推送（Registry 只推给当时已注册的会话）。
    for (EServerType T : EnumerateBusinessServerTypes())
    {
        PendingListTypes_.insert(T);
    }
    for (EServerType T : PendingListTypes_)
    {
        TByteArray ListPacket;
        RegistryProtocol::BuildRegistryListEndpointsPacket(T, ListPacket);
        SendToRegistry(ListPacket);
    }
    PendingListTypes_.clear();
}

TSharedPtr<MServerConnection> MEndpointCache::GetOrConnect(EServerType TargetServerType)
{
    std::lock_guard<std::mutex> Lock(Mutex_);
    auto It = Endpoints_.find(TargetServerType);
    if (It == Endpoints_.end() || It->second.empty())
    {
        if (MLogIsDebugBuild())
        {
            LOG_WARN("MEndpointCache: no endpoints for type=%s",
                     GetServerTypeDisplayName(TargetServerType));
        }
        else
        {
            LOG_DEBUG("MEndpointCache: no endpoints for type=%s",
                      GetServerTypeDisplayName(TargetServerType));
        }
        return nullptr;
    }
    // round-robin：每次调用递增 index——放在 FRegistryClient 成员里，多进程
    // 共享单例时各 Service 独立累计起点，互不干扰。
    const uint32 Start = Registry_.RoundRobinIdx++;
    for (size_t i = 0; i < It->second.size(); ++i)
    {
        const FServiceEndpoint& Ep = It->second[(Start + i) % It->second.size()];
        if (!Ep.bHealthy) continue;
        // 跳过本进程自己的端点——否则 round-robin 可能选中自己造成自连循环
        //（EchoA 转发 2001 时连回自己的 7001 → 再转发 → 连接不稳定）。
        if (Ep.ServerId == Registry_.LocalServerId) continue;
        auto PoolIt = ConnectionPool_.find(Ep.ServerId);
        if (PoolIt != ConnectionPool_.end() && PoolIt->second && PoolIt->second->IsConnected())
        {
            return PoolIt->second;
        }
        TSharedPtr<MServerConnection> Conn = LazyConnect(Ep);
        if (Conn)
        {
            return Conn;
        }
    }
    if (MLogIsDebugBuild())
    {
        LOG_WARN("MEndpointCache: all endpoints unhealthy for type=%s",
                 GetServerTypeDisplayName(TargetServerType));
    }
    else
    {
        LOG_DEBUG("MEndpointCache: all endpoints unhealthy for type=%s",
                  GetServerTypeDisplayName(TargetServerType));
    }
    return nullptr;
}

void MEndpointCache::OnEndpointChange(EServerType ServerType, uint64 Seq, const TVector<FServiceEndpoint>& NewEndpoints)
{
    std::lock_guard<std::mutex> Lock(Mutex_);
    auto SeqIt = LastSeq_.find(ServerType);
    if (SeqIt != LastSeq_.end() && Seq < SeqIt->second)
    {
        if (MLogIsDebugBuild())
        {
            LOG_FATAL("MEndpointCache: stale push ServerType=%s Seq=%llu LastSeq=%llu",
                      GetServerTypeDisplayName(ServerType),
                      static_cast<unsigned long long>(Seq),
                      static_cast<unsigned long long>(SeqIt->second));
        }
        LOG_WARN("MEndpointCache: stale push rejected ServerType=%s Seq=%llu LastSeq=%llu",
                 GetServerTypeDisplayName(ServerType),
                 static_cast<unsigned long long>(Seq),
                 static_cast<unsigned long long>(SeqIt->second));
        return;
    }
    LastSeq_[ServerType] = Seq;
    Endpoints_[ServerType] = NewEndpoints;

    TVector<uint32> Live;
    Live.reserve(NewEndpoints.size());
    for (const FServiceEndpoint& Ep : NewEndpoints)
    {
        // 把端点携带的 actor 注册到 MActorRouter——跨实例 actor 路由依赖它：
        // EchoA 收到 EchoB 的 EndpointChange（ActorIds 含 2001）后，FindActor(2001)
        // 才能命中（否则 SendToActor 表 miss → DefaultServer=Unknown → actor_route_invalid）。
        // 只注册"未存在"的 actor：本进程已注册的本地 actor（ServerType=Unknown，
        // IsActorLocal 判定依据）不能被端点信息覆盖成实际服务类型。
        for (uint64 ActorId : Ep.ActorIds)
        {
            if (MActorRouter::Get().FindActor(ActorId).ActorId == 0)
            {
                MActorRouter::Get().RegisterActor(ActorId, Ep.ServerType);
            }
        }
        Live.push_back(Ep.ServerId);
    }
    PurgeDisappeared(Live);
}

void MEndpointCache::Tick(float DeltaTime)
{
    std::lock_guard<std::mutex> Lock(Mutex_);
    if (!Registry_.bConnected)
    {
        Registry_.ReconnectTimer += DeltaTime;
        if (Registry_.ReconnectTimer >= 5.0f)
        {
            Registry_.ReconnectTimer = 0.0f;
            TryConnectRegistry();
        }
        return;
    }

    // 心跳发送（5s 节拍）——timer 累计放进 FRegistryClient 成员，多进程
    // 共享单例时各 Service 独立累计。
    Registry_.HeartbeatTimer += DeltaTime;
    if (Registry_.HeartbeatTimer >= 5.0f && Registry_.LocalServerId != 0)
    {
        Registry_.HeartbeatTimer = 0.0f;
        TByteArray Packet;
        RegistryProtocol::BuildRegistryHeartbeatPacket(Registry_.LocalServerId, NowMs(), Packet);
        SendToRegistry(Packet);
    }

    // 出站业务连接（LazyConnect 建的 MServerConnection，如 Gateway→Echo、
    // Echo→Echo 的服务器间链路）收包驱动：它们没有 EventLoop 收包回调
    // （只有入站走 OnAccept→RegisterConnection），MServerConnection::Tick
    // 里的 ProcessRecv（Transport->ReceivePacket → HandlePacket → 服务器间
    // 响应分发）没有其他调用点——这里每帧驱动，否则对端响应永远收不到。
    for (auto& KV : ConnectionPool_)
    {
        if (KV.second)
        {
            KV.second->Tick(DeltaTime);
        }
    }
}

void MEndpointCache::DeregisterAndShutdown()
{
    std::lock_guard<std::mutex> Lock(Mutex_);
    if (Registry_.bConnected && Registry_.LocalServerId != 0)
    {
        TByteArray Packet;
        RegistryProtocol::BuildRegistryDeregisterPacket(Registry_.LocalServerId, Packet);
        SendToRegistry(Packet);
    }
    if (Registry_.ConnId != 0 && EventLoop_)
    {
        EventLoop_->UnregisterConnection(Registry_.ConnId);
    }
    Registry_.ConnId = 0;
    if (Registry_.Conn && Registry_.Conn->IsConnected())
    {
        Registry_.Conn->Close();
    }
    Registry_.bConnected = false;
    Registry_.Conn.reset();
    ConnectionPool_.clear();
}

void MEndpointCache::HandleRegistryPacket(const TByteArray& Packet)
{
    if (Packet.empty()) return;
    const uint8 PacketType = Packet[0];
    const TByteArray Payload(Packet.begin() + 1, Packet.end());
    switch (static_cast<EServiceRegistryMessageType>(PacketType))
    {
    case EServiceRegistryMessageType::EndpointChange:
    {
        EServerType Type = EServerType::Unknown;
        uint64 Seq = 0;
        TVector<FServiceEndpoint> Eps;
        if (!RegistryProtocol::ParseRegistryEndpointChangePacket(Payload, Type, Seq, Eps))
        {
            LOG_WARN("MEndpointCache: malformed EndpointChange");
            return;
        }
        OnEndpointChange(Type, Seq, Eps);
        break;
    }
    case EServiceRegistryMessageType::Ack:
    {
        EServiceRegistryResult Status = EServiceRegistryResult::Ok;
        MString Message;
        if (!RegistryProtocol::ParseRegistryAckPacket(Payload, Status, Message))
        {
            LOG_WARN("MEndpointCache: malformed Ack");
            return;
        }
        if (Status != EServiceRegistryResult::Ok)
        {
            LOG_WARN("MEndpointCache: registry ack status=%u message=%s",
                     static_cast<unsigned>(Status), Message.c_str());
        }
        break;
    }
    default:
        LOG_WARN("MEndpointCache: unexpected packet type=%u", static_cast<unsigned>(PacketType));
        break;
    }
}

void MEndpointCache::TryConnectRegistry()
{
    if (Registry_.Addr.empty() || Registry_.Port == 0) return;
    if (Registry_.bConnected) return;
    if (!EventLoop_)
    {
        LOG_ERROR("MEndpointCache: EventLoop_ not attached; cannot connect registry");
        return;
    }
    if (!MSocket::EnsureInit())
    {
        LOG_ERROR("MEndpointCache: socket init failed");
        return;
    }
    TSharedPtr<MTcpConnection> Tcp = MTcpConnection::ConnectTo(SSocketAddress(Registry_.Addr, Registry_.Port), 3.0f);
    if (!Tcp || !Tcp->IsConnected())
    {
        if (MLogIsDebugBuild())
        {
            LOG_FATAL("MEndpointCache: failed to connect registry at %s:%u (DEBUG abort)",
                      Registry_.Addr.c_str(), static_cast<unsigned>(Registry_.Port));
        }
        LOG_ERROR("MEndpointCache: failed to connect registry at %s:%u — will retry in 5s",
                  Registry_.Addr.c_str(), static_cast<unsigned>(Registry_.Port));
        return;
    }
    Registry_.Conn = Tcp;
    Registry_.bConnected = true;
    // 把连接挂到 Service 进程内的 NetEventLoop 上轮询收包。
    // PacketType 1 字节 + payload 的 envelope 已经在 MTcpConnection::ReceivePacket
    // 那边按 length-prefixed 拆过；我们这里收一个完整 packet 后再按 1-byte type
    // 分发到 HandleRegistryPacket。
    Registry_.ConnId = MUniqueIdGenerator::Generate();
    const uint64 ConnId = Registry_.ConnId;
    EventLoop_->RegisterConnection(
        ConnId,
        Tcp,
        [this](uint64 /*Id*/, const TByteArray& Packet)
        {
            HandleRegistryPacket(Packet);
        },
        [this](uint64 Id)
        {
            std::lock_guard<std::mutex> Lock(Mutex_);
            if (Id != Registry_.ConnId) return;
            OnRegistryDisconnected();
        });
    OnRegistryConnected();
    LOG_INFO("MEndpointCache: connected to registry at %s:%u",
             Registry_.Addr.c_str(), static_cast<unsigned>(Registry_.Port));
}

void MEndpointCache::OnRegistryConnected()
{
    // 如果已有 LocalInfo，重新 Register
    if (Registry_.LocalServerId != 0)
    {
        FServiceEndpoint Self;
        Self.ServerId = Registry_.LocalServerId;
        Self.ServerType = Registry_.LocalServerType;
        Self.Address = Registry_.LocalAddress;
        Self.Port = Registry_.LocalPort;
        Self.ActorIds = Registry_.LocalActorIds;
        TByteArray Packet;
        if (RegistryProtocol::BuildRegistryRegisterPacket(Self, Packet))
        {
            SendToRegistry(Packet);
        }
    }
    for (EServerType T : PendingListTypes_)
    {
        TByteArray Packet;
        RegistryProtocol::BuildRegistryListEndpointsPacket(T, Packet);
        SendToRegistry(Packet);
    }
    PendingListTypes_.clear();
}

void MEndpointCache::OnRegistryDisconnected()
{
    if (Registry_.ConnId != 0 && EventLoop_)
    {
        EventLoop_->UnregisterConnection(Registry_.ConnId);
    }
    Registry_.ConnId = 0;
    Registry_.Conn.reset();
    Registry_.bConnected = false;
    Registry_.ReconnectTimer = 0.0f;
    LOG_WARN("MEndpointCache: registry disconnected");
}

void MEndpointCache::SendToRegistry(const TByteArray& Packet)
{
    if (!Registry_.bConnected || !Registry_.Conn || !Registry_.Conn->IsConnected()) return;
    Registry_.Conn->Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

TSharedPtr<MServerConnection> MEndpointCache::LazyConnect(const FServiceEndpoint& Ep)
{
    // PoC 同主机：Address="0.0.0.0" 走 loopback。Service 端按 ServerType 解析
    // endpoint class name。
    const char* EndpointClass = GetServerEndpointClassName(Ep.ServerType);
    if (!EndpointClass)
    {
        LOG_WARN("MEndpointCache: no endpoint class for type=%s",
                 GetServerTypeDisplayName(Ep.ServerType));
        return nullptr;
    }
    const uint32 PeerServerId = MUniqueIdGenerator::Generate();
    SServerConnectionConfig PeerConfig(
        PeerServerId,
        Ep.ServerType,
        EndpointClass,
        "127.0.0.1",  // PoC: loopback regardless of registry Address field
        Ep.Port);
    TSharedPtr<MServerConnection> PeerConn = MakeShared<MServerConnection>(PeerConfig);
    if (!PeerConn->Connect())
    {
        return nullptr;
    }
    ConnectionPool_[Ep.ServerId] = PeerConn;
    // P2: wire inbound packet dispatch (MT_FunctionCall/Response).
    AttachDispatchToConnection(PeerConn, ServiceInstance_);
    return PeerConn;
}

void MEndpointCache::PurgeDisappeared(const TVector<uint32>& LiveServerIds)
{
    // 现有 pool entries 不在 LiveServerIds → 保留连接（不销毁）。
    // 注意：不能 erase——进行中的 CallServerFunction 的 LivenessProbe 持有
    // WeakConnection，销毁会让它在等待响应期间失效（server_call_disconnected）。
    // 连接是否真正断开由 MServerConnection::Tick（ProcessRecv 检测 EOF）自愈。
    // 仅清理已断开的连接（IsConnected false），避免池无限增长。
    for (auto It = ConnectionPool_.begin(); It != ConnectionPool_.end();)
    {
        bool bAlive = false;
        for (uint32 Id : LiveServerIds)
        {
            if (It->first == Id) { bAlive = true; break; }
        }
        if (!bAlive && (!It->second || !It->second->IsConnected()))
        {
            It = ConnectionPool_.erase(It);
        }
        else
        {
            ++It;
        }
    }
}
