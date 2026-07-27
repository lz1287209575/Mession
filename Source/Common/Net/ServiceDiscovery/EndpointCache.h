#pragma once

#include "Common/Net/ServerConnection.h"
#include "Common/Net/ServiceDiscovery/Endpoint.h"
#include "Common/Net/ServiceDiscovery/RegistryProtocol.h"
#include "Common/IO/Socket/Socket.h"
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
class MEndpointCache
{
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

    // P2: 安装业务的 MObject* 用于接收 MT_FunctionCall / MT_FunctionResponse。
    // Setup at Service::Init after AttachEventLoop. Non-owning — Service 进程级单例。
    void SetServiceInstance(MObject* ServiceInstance);
    MObject* GetServiceInstance() const;

    // 处理 Registry 推过来的 EndpointChange。Seq 必须 >= LastSeq_ 否则拒绝。
    void OnEndpointChange(EServerType ServerType, uint64 Seq, const TVector<FServiceEndpoint>& NewEndpoints);

    void Tick(float DeltaTime);
    void DeregisterAndShutdown();

    // 由 Service 进程内 EventLoop 收到 Registry 包时回调
    void HandleRegistryPacket(const TByteArray& Packet);

private:
    struct FRegistryClient
    {
        MString Addr;
        uint16 Port = 0;
        TSharedPtr<MTcpConnection> Conn;      // 连接到 Registry 的 socket
        uint64 ConnId = 0;                    // EventLoop 注册的 ConnId
        TByteArray RecvBuffer;                // 上层 envelope 累积
        bool bConnected = false;
        float ReconnectTimer = 0.0f;
        float HeartbeatTimer = 0.0f;          // 心跳发送节拍（5s）——每个进程独立累计
        uint32 RoundRobinIdx = 0;             // GetOrConnect 的 round-robin 起点
        uint64 LocalServerId = 0;             // Register 成功后填
        EServerType LocalServerType = EServerType::Unknown;
        TVector<uint64> LocalActorIds;
        MString LocalAddress;
        uint16 LocalPort = 0;
    };

    void TryConnectRegistry();
    void OnRegistryConnected();
    void OnRegistryDisconnected();
    void DispatchRegistryPacket(const TByteArray& Packet);
    void SendToRegistry(const TByteArray& Packet);
    TSharedPtr<MServerConnection> LazyConnect(const FServiceEndpoint& Ep);
    void PurgeDisappeared(const TVector<uint32>& LiveServerIds);

    FRegistryClient Registry_;
    TMap<EServerType, TVector<FServiceEndpoint>> Endpoints_;
    TMap<uint32, TSharedPtr<MServerConnection>> ConnectionPool_;
    TMap<EServerType, uint64> LastSeq_;
    TSet<EServerType> PendingListTypes_;
    std::mutex Mutex_;
    MNetEventLoop* EventLoop_ = nullptr;      // not owned
    MObject* ServiceInstance_ = nullptr;      // P2: not owned; process-lifetime
};
