#pragma once

#include "Common/IO/Socket/Socket.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Net/ServiceDiscovery/Endpoint.h"
#include "Common/Net/ServiceDiscovery/RegistryProtocol.h"
#include "Common/Runtime/EventLoop/NetEventLoop.h"

#include <mutex>

/**
 * MEndpointCache - Service-side service-discovery cache.
 *
 * Holds:
 *   - One Registry TCP client connection (reconnecting on failure)
 *   - Per-ServerType endpoint snapshots received from Registry pushes
 *   - A connection pool of MServerConnection instances keyed by ServerId
 *
 * Lifecycle (called from MEchoService::Init / MGatewayServer::Init):
 *   1. MEndpointCache::Get().AttachEventLoop(&EventLoop)   // once at startup
 *   2. MEndpointCache::Get().BindRegistry(Addr, Port)
 *   3. MEndpointCache::Get().RegisterLocal(SelfEndpoint)
 *   4. Every TickBackends() call: MEndpointCache::Get().Tick(DeltaTime)
 *   5. On shutdown: MEndpointCache::Get().DeregisterAndShutdown()
 *
 * The TCP socket to Registry reuses the standard MTcpConnection + MNetEventLoop
 * dispatcher in the Service's own NetServerBase — see the wiring in
 * EchoService.cpp / GatewayServer.cpp Task 8.
 */
class MEndpointCache {
    public:
    static MEndpointCache& Get();

    // 把 Service 进程内的 NetEventLoop 引用交给 Cache：连接 Registry 后
    // 通过它 poll + 收包 + 回调 HandleRegistryPacket。AttachEventLoop
    // 必须在 BindRegistry 之前调用，否则首次 TCP 连接成功后没有 EventLoop
    // 收包就 ping 不通。
    void AttachEventLoop(MNetEventLoop* EventLoop);

    void BindRegistry(const MString& Addr, uint16 Port);
    void RegisterLocal(const FServiceEndpoint& Self);

    // 主业务路径：MRpcChannel::Call / SendToActor 通过这里。空表 / 全
    // unhealthy → 返回 nullptr，调用方走 RPC 错误路径。
    TSharedPtr<MServerConnection> GetOrConnect(EServerType TargetServerType);

    // 多 Reactor 扩展(P5):per-Sub 版本的 GetOrConnect,按 SubId 分桶拿 connection。
    // 不同 Sub 拿同一 ServerType 的 connection 互不影响(各自的池)。
    // 锁粒度:本 Sub 自己的 Mutex(从全局锁降到 Sub 数)。
    // InSubId 必须 < 当前 Sub 数(调用方负责,通常 = SubPool->PickSub(...))
    TSharedPtr<MServerConnection> GetOrConnect(EServerType TargetServerType, uint32 SubId);

    // P2: 安装业务的 MObject* 用于接收 MT_FunctionCall / MT_FunctionResponse。
    // Setup at Service::Init after AttachEventLoop. Non-owning — Service 进程级单例。
    void     SetServiceInstance(MObject* ServiceInstance);
    MObject* GetServiceInstance() const;

    // 多 Reactor 扩展(P5):per-Sub 反射上下文。每个 Sub 持有自己的 MObject*,
    // 避免跨 Sub 共享反射元数据时锁竞争。启动期由 MServiceMain 一次性注入,
    // 运行期只读。
    void     SetServiceInstance(uint32 SubId, MObject* ServiceInstance);
    MObject* GetServiceInstance(uint32 SubId) const;

    // 处理 Registry 推过来的 EndpointChange。Seq 必须 >= LastSeq 否则拒绝。
    void OnEndpointChange(EServerType ServerType, uint64 Seq, const TVector<FServiceEndpoint>& NewEndpoints);

    void Tick(float DeltaTime);
    void DeregisterAndShutdown();

    // 由 Service 进程内 EventLoop 收到 Registry 包时回调
    void HandleRegistryPacket(const TByteArray& Packet);

    private:
    struct FRegistryClient {
        MString                    Addr;
        uint16                     Port = 0;
        TSharedPtr<MTcpConnection> Conn;       // 连接到 Registry 的 socket
        uint64                     ConnId = 0; // EventLoop 注册的 ConnId
        TByteArray                 RecvBuffer; // 上层 envelope 累积
        bool                       bConnected      = false;
        float                      ReconnectTimer  = 0.0f;
        float                      HeartbeatTimer  = 0.0f; // 心跳发送节拍（5s）——每个进程独立累计
        uint32                     RoundRobinIdx   = 0;    // GetOrConnect 的 round-robin 起点
        uint64                     LocalServerId   = 0;    // Register 成功后填
        EServerType                LocalServerType = EServerType::Unknown;
        TVector<uint64>            LocalActorIds;
        MString                    LocalAddress;
        uint16                     LocalPort = 0;
    };

    // 多 Reactor 扩展(P5):每个 Sub reactor 一份连接池.
    //
    // Sub 之间的 ConnectionPool 独立,锁竞争下降到1/N。
    // 同一 ServerId 可能出现在多个 Sub 的桶里(因为池独立),
    // 这是有意的:让每个 Sub 都能就近复用已建立的连接。
    //
    // 选 endpoint + 复用/新建的逻辑在 MEndpointCache::GetOrConnect(Type, SubId) 内联,
    // 因为它需要访问 Endpoints / Registry / LazyConnect (私有) — SPerSubPool 只持数据。
    struct SPerSubPool {
        TMap<uint32, TSharedPtr<MServerConnection>> Connections;
        std::mutex                                  Mutex;
        uint32                                      RoundRobinIdx = 0;
    };

    void                          TryConnectRegistry();
    void                          OnRegistryConnected();
    void                          OnRegistryDisconnected();
    void                          DispatchRegistryPacket(const TByteArray& Packet);
    void                          SendToRegistry(const TByteArray& Packet);
    TSharedPtr<MServerConnection> LazyConnect(const FServiceEndpoint& Ep);
    void                          PurgeDisappeared(const TVector<uint32>& LiveServerIds);

    FRegistryClient                              Registry;
    TMap<EServerType, TVector<FServiceEndpoint>> Endpoints;
    TMap<uint32, TSharedPtr<MServerConnection>>  ConnectionPool;
    TMap<EServerType, uint64>                    LastSeq;
    TSet<EServerType>                            PendingListTypes;
    // mutable: const 成员函数(GetServiceInstance(SubId)) 也能安全加锁
    mutable std::mutex Mutex;
    MNetEventLoop*     EventLoop       = nullptr; // not owned
    MObject*           ServiceInstance = nullptr; // P2: not owned; process-lifetime

    // 多 Reactor 扩展(P5):per-Sub 反射上下文 + per-Sub 连接池. 启动期注入,运行期只读.
    // PerSubPools 的 Mutex 是各 Sub 自己独立,全局 Mutex 保护 PerSubPools/PerSubService 自身.
    TMap<uint32, MObject*>    PerSubService;
    TMap<uint32, SPerSubPool> PerSubPools;
};
