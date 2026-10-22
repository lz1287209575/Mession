/**
 * @file EndpointCache.h
 * @brief MEndpointCache - Service 侧的服务发现缓存(进程级单例)。
 *
 * @detail 持有三部分状态:
 *   - 一条到 Registry 的 TCP 客户端连接(断开后按 5s 节奏自动重连);
 *   - 按 ServerType 组织的端点快照(来自 Registry 主动推送 EndpointChange);
 *   - 按 ServerId 索引的 MServerConnection 连接池(懒建 + 复用)。
 * 本类不解析地址:PoC 阶段一律用 loopback + 端点里的 Port 连(见 LazyConnect)。
 *
 * 线程模型:公开入口内部加锁(全局 Mutex 或 per-Sub 池锁);EventLoop 与
 * ServiceInstance 都是**外部所有**的裸指针,只在进程生命周期内有效。
 */
#pragma once

#include "Common/IO/Socket/Socket.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Net/ServiceDiscovery/Endpoint.h"
#include "Common/Net/ServiceDiscovery/RegistryProtocol.h"
#include "Common/Runtime/EventLoop/NetEventLoop.h"

#include <mutex>

/**
 * @brief MEndpointCache - Service 进程内的服务发现缓存单例。
 *
 * @detail 生命周期(GatewayServer / EchoService / PlayerService 的 Init 都按这个顺序):
 *   1. MEndpointCache::Get().AttachEventLoop(&EventLoop);   // 必须先做:Registry 连接靠它收包
 *   2. MEndpointCache::Get().SetServiceInstance(this);      // 安装业务反射入口
 *   3. MEndpointCache::Get().BindRegistry(Addr, Port);      // 内部立刻尝试连接
 *   4. MEndpointCache::Get().RegisterLocal(SelfEndpoint);   // 注册 + 主动拉端点
 *   5. 每帧:MEndpointCache::Get().Tick(DeltaTime);          // 心跳 / 重连 / 驱动出站连接收包
 *   6. 退出:MEndpointCache::Get().DeregisterAndShutdown();
 * 到 Registry 的 TCP socket 复用标准 MTcpConnection + Service 自己的 MNetEventLoop 分发。
 */
class MEndpointCache {
    public:
    /**
     * @brief Get - 进程级单例(首次调用时构造)。
     */
    static MEndpointCache& Get();

    /**
     * @brief AttachEventLoop - 把 Service 进程内的 NetEventLoop 交给本 Cache。
     * @param EventLoop 非拥有指针,需在 Cache 使用期内一直有效
     *
     * @detail 连接 Registry 后靠这个 loop 来 poll + 收包 + 回调 HandleRegistryPacket。
     * 必须在 BindRegistry 之前调用:否则首连虽然会成功,但没有 loop 收包,心跳/推送都收不到。
     */
    void AttachEventLoop(MNetEventLoop* EventLoop);

    /**
     * @brief BindRegistry - 记录 Registry 地址并立即尝试建立连接。
     * @param Addr Registry 主机(仅作记录/日志用,PoC 下真正连的是这个地址)
     * @param Port Registry 端口
     *
     * @detail 地址或端口为空/0 时不做任何事;失败只记日志,由 Tick 的重连节奏继续尝试。
     */
    void BindRegistry(const MString& Addr, uint16 Port);

    /**
     * @brief RegisterLocal - 记录本实例信息并向 Registry 注册。
     * @param Self 通常来自 MakeLocalEndpoint(Config)
     *
     * @detail 流程:缓存本地信息 → 连接已建立则发 Register,并立刻对本进程关心的业务类型
     * 补发一次 ListEndpoints。补发的原因:OnRegistryConnected(连接建立时)执行时
     * PendingListTypes 还是空的,首次启动的 RegisterLocal 发生在它之后,不补发就会错过
     * 先注册服务(如 Echo)的 EndpointChange 推送 —— Registry 只推给当时已注册的会话。
     * Registry 未连接时不重试:非 NDEBUG 构建下多写一条 LOG_FATAL(Critical 级,经异步
     * 队列落盘,**不会** abort —— 只有 LOG_FATAL_EX 才走 coredump 路径),随后统一记 ERROR
     * 并"降级继续"(服务仍可启动,只是拿不到远端端点)。
     */
    void RegisterLocal(const FServiceEndpoint& Self);

    /**
     * @brief UpdateLocalActorIds - 全量更新本进程的 actor 列表并上报 Registry。
     * @param InActorIds 新的完整 ActorId 列表(全量语义)
     *
     * @detail 调用点:MActorSystem 在 actor 上线/下线后汇总后调用。
     * Registry 侧据此更新端点并推送 EndpointChange,对端 MActorRouter 再注册/注销远端路由。
     * Registry 未连接、或本进程还没注册成功(ServerId == 0)时只更新本地副本、不发包。
     */
    void UpdateLocalActorIds(const TVector<uint64>& InActorIds);

    /**
     * @brief GetOrConnect - 主业务路径取连接(MRpcChannel::Call / SendToActor 都走这里)。
     * @param TargetServerType 目标业务类型
     * @return 可用的服务器间连接;端点表为空或无一可用(不健康/只有自己)时返回 nullptr
     *
     * @detail 选点顺序:按 RoundRobinIdx 起点遍历该类型的端点,跳过 bHealthy == false
     * 与 ServerId == 本进程的端点(否则会自连形成转发环路),优先复用池里已连接的
     * MServerConnection,否则 LazyConnect 新建。
     * 返回 nullptr 时调用方走 RPC 错误路径。
     */
    TSharedPtr<MServerConnection> GetOrConnect(EServerType TargetServerType);

    /**
     * @brief GetOrConnect - per-Sub 版本:按 SubId 分桶取连接。
     * @param TargetServerType 目标业务类型
     * @param SubId 所属 Sub reactor 序号;必须 < 当前 Sub 数(调用方负责,通常来自 SubPool->PickSub())
     * @return 该 Sub 桶内的连接;无可选端点时 nullptr
     *
     * @detail 多 Reactor 扩展(P5):不同 Sub 各自持有一份连接池与 round-robin 起点,
     * 同一 ServerId 因此可能在多个 Sub 桶里各有一条连接 —— 这是有意的,让每个 Sub 就近复用。
     * 锁粒度从全局 Mutex 降到 per-Sub Mutex。端点是全进程共享的:
     * 同一 ServerType 的端点表变化对所有 Sub 同时生效。
     * 端点表与本机 id 会在全局锁内复制出快照；随后只持该 Sub 自己的池锁完成
     * 选点和建连，不会与 EndpointChange 的写入产生数据竞争。
     */
    TSharedPtr<MServerConnection> GetOrConnect(EServerType TargetServerType, uint32 SubId);

    /**
     * @brief SetServiceInstance - 安装业务的 MObject 反射入口(进程级)。
     * @param ServiceInstance 非拥有指针;进程生命周期内有效
     *
     * @detail 用于接收服务器间 MT_FunctionCall / MT_FunctionResponse。在
     * Service::Init 里、AttachEventLoop 之后调用;同时会把已有连接池里连接的
     * 入站包回调重新挂到这个实例上(补偿"连接先建立、实例后安装"的顺序)。
     */
    void SetServiceInstance(MObject* ServiceInstance);

    /**
     * @brief GetServiceInstance - 取进程级反射入口。
     * @return 未安装时返回 nullptr
     */
    MObject* GetServiceInstance() const;

    /**
     * @brief SetServiceInstance - per-Sub 反射入口。
     * @param SubId 所属 Sub reactor 序号
     * @param ServiceInstance 非拥有指针;传 nullptr 时记 WARN 并直接返回(不做任何修改)
     *
     * @detail 多 Reactor 扩展(P5):每个 Sub 持有自己的 MObject*,避免跨 Sub 共享反射元数据
     * 时产生锁竞争。启动期由 MServiceMain 一次性注入,运行期只读;注入后会把该 Sub 池里
     * 已建立的连接重新挂上入站分发回调。
     */
    void SetServiceInstance(uint32 SubId, MObject* ServiceInstance);

    /**
     * @brief GetServiceInstance - 取指定 Sub 的反射入口。
     * @return 未安装时返回 nullptr
     *
     * @detail 虽然运行期"只读",仍加锁:TMap 不是线程安全的,读也要与 SetServiceInstance 互斥。
     */
    MObject* GetServiceInstance(uint32 SubId) const;

    /**
     * @brief OnEndpointChange - 应用 Registry 推来的端点快照。
     * @param ServerType    本次快照针对的业务类型
     * @param Seq           推送序号;小于等于已记录的 LastSeq 时被拒绝(丢弃并记 WARN)
     * @param NewEndpoints  该类型的完整快照(全量替换,非增量)
     *
     * @detail 除替换快照外还有两个副作用:
     *   1. 把端点携带的 ActorIds 注册进 MActorRouter(只注册尚未登记的,避免把本进程
     *      本地 actor 的归属覆盖成远端类型),否则 SendToActor 会因表 miss 而路由失败;
     *   2. 对每个 actor 调用 MActorSystem::OnActorEndpointRecovered,让端点恢复时重发
     *      outbox 里积压的消息。
     * 最后调用 PurgeDisappeared 清掉已消失且确实断开的池条目。
     */
    void OnEndpointChange(EServerType ServerType, uint64 Seq, const TVector<FServiceEndpoint>& NewEndpoints);

    /**
     * @brief Tick - 每帧驱动:重连 / 心跳 / 出站连接收包。
     * @param DeltaTime 距上一帧的秒数
     *
     * @detail 未连接 Registry 时累计 ReconnectTimer,满 5s 重连一次并直接返回;
     * 已连接时累计 5s 心跳(仅在本进程已注册成功后发),并逐个 Tick 连接池里的
     * MServerConnection —— 这些出站连接没有 EventLoop 收包回调,不在这里驱动就永远
     * 收不到对端响应(入站连接走 OnAccept,不依赖本函数)。
     */
    void Tick(float DeltaTime);

    /**
     * @brief DeregisterAndShutdown - 注销并拆掉 Registry 连接与连接池。
     *
     * @detail 已注册则先发 Deregister;随后从 EventLoop 注销连接、关闭并释放 Conn、
     * 清空连接池。幂等,可在退出路径重复调用。
     */
    void DeregisterAndShutdown();

    /**
     * @brief HandleRegistryPacket - 处理来自 Registry 的一个完整包。
     * @param Packet 完整包(1 字节类型 + payload),空包直接忽略
     *
     * @detail 只处理服务端会发的两类:EndpointChange(转 OnEndpointChange)与
     * Ack(仅日志);其它类型记 WARN。Register/Heartbeat 等是反向(Service → Registry)的包,
     * 不在这里。由 EventLoop 的收包回调驱动,自身不加锁。
     */
    void HandleRegistryPacket(const TByteArray& Packet);

    private:
    /**
     * @brief FRegistryClient - 与 Registry 的连接状态与本地注册信息。
     *
     * @detail 全部字段按"单线程 loop 内访问 + 全局 Mutex 保护公开入口"的方式使用:
     * 定时器(ReconnectTimer / HeartbeatTimer)只在 Tick 里累计,Tick 由每个 Service
     * 各调一次,因此是每进程独立节拍。
     */
    struct FRegistryClient {
        MString                    Addr;
        uint16                     Port = 0;
        TSharedPtr<MTcpConnection> Conn;       // 连接到 Registry 的 socket
        uint64                     ConnId = 0; // EventLoop 注册的 ConnId
        TByteArray                 RecvBuffer; // 上层 envelope 累积
        bool                       bConnected      = false;
        float                      ReconnectTimer  = 0.0f;
        float                      HeartbeatTimer  = 0.0f; // 心跳发送节拍(5s),每进程独立累计
        uint32                     RoundRobinIdx   = 0;    // 进程级 GetOrConnect 的 round-robin 起点
        uint64                     LocalServerId   = 0;    // Register 成功后填;0 = 尚未注册
        EServerType                LocalServerType = EServerType::Unknown;
        TVector<uint64>            LocalActorIds;
        MString                    LocalAddress;
        uint16                     LocalPort = 0;
    };

    /**
     * @brief SPerSubPool - 单个 Sub reactor 的连接桶。
     *
     * @detail 多 Reactor 扩展(P5):Sub 之间连接池互相独立,锁竞争降到 1/N;
     * 同一 ServerId 可能同时出现在多个 Sub 的桶里(池独立),这是有意为之。
     * 只持数据:"选端点 + 复用/新建"的逻辑留在 MEndpointCache::GetOrConnect(Type, SubId) 内联,
     * 因为它需要访问 Endpoints / Registry / LazyConnect 这些私有成员。
     */
    struct SPerSubPool {
        TMap<uint32, TSharedPtr<MServerConnection>> Connections;
        std::mutex                                  Mutex;
        uint32                                      RoundRobinIdx = 0;
    };

    /**
     * @brief TryConnectRegistry - 建立到 Registry 的连接并挂到 EventLoop 上。
     *
     * 前置条件:EventLoop 已 Attach、Addr/Port 已 Bind、当前未连接。
     * 连接超时 3s;连接失败时非 NDEBUG 构建会额外写 LOG_FATAL(Critical 级日志,不 abort),
     * 随后交由 Tick 的 5s 节奏重试。
     * 成功后会立即 OnRegistryConnected()(补发注册与未完成的 ListEndpoints)。
     */
    void TryConnectRegistry();

    /**
     * @brief OnRegistryConnected - 连接(或重连)成功后的补发动作。
     *
     * @detail 已有本地信息(LocalServerId != 0)就重发 Register,并补发 PendingListTypes
     * 里挂着的 ListEndpoints 请求,然后清空该集合。
     */
    void OnRegistryConnected();

    /**
     * @brief OnRegistryDisconnected - 连接断开后的清理。
     *
     * @detail 从 EventLoop 注销并释放连接、清 bConnected、把 ReconnectTimer 归零
     * (让下一次 Tick 重新开始 5s 计时)。由 EventLoop 的断线回调触发。
     */
    void OnRegistryDisconnected();

    /**
     * @brief DispatchRegistryPacket - 按包首类型分发 Registry 包。
     *
     * @detail 现状:仅有声明,既无定义也无调用点(实际分发逻辑内联在 HandleRegistryPacket)。
     * 保留作接口占位。
     */
    void DispatchRegistryPacket(const TByteArray& Packet);

    /**
     * @brief SendToRegistry - 发送一个已组装好的包。
     *
     * @detail 未连接或连接不可用时静默丢弃(不排队、不重试);发送本身走 MTcpConnection,
     * 入队失败也无法从这里获知。
     */
    void SendToRegistry(const TByteArray& Packet);

    /**
     * @brief LazyConnect - 为某个端点建立服务器间连接并挂上入站分发。
     * @param Ep 目标端点(只用 ServerType 与 Port,Address 被忽略)
     * @return 已连接的 MServerConnection;找不到该类型的 endpoint class 或连接失败时 nullptr
     *
     * @detail PoC 同主机:无论 Registry 报什么 Address,一律连 "127.0.0.1" + Ep.Port。
     * 新连接会写进 ConnectionPool[Ep.ServerId](调用方 per-Sub 版本会再放进自己的桶),
     * 并 AttachDispatchToConnection 把入站 MT_FunctionCall/MT_FunctionResponse 接到
     * 当前 ServiceInstance（进程级版本,per-Sub 版本由调用方另行覆盖）。
     */
    TSharedPtr<MServerConnection> LazyConnect(const FServiceEndpoint& Ep);

    /**
     * @brief PurgeDisappeared - 清理池中已消失且确实断开的连接。
     * @param LiveServerIds 本轮快照里的存活 ServerId 列表
     *
     * @detail 只删"不在存活列表 **且** 未连接"的条目:不能只按列表删除,因为进行中的
     * CallServerFunction 的活性探测持有 WeakConnection,提前销毁会让它在等响应期间失效;
     * 连接是否真的断了由 MServerConnection::Tick 自愈(等 ProcessRecv 检测到 EOF)。
     */
    void PurgeDisappeared(const TVector<uint32>& LiveServerIds);

    FRegistryClient                              Registry;
    TMap<EServerType, TVector<FServiceEndpoint>> Endpoints;
    TMap<uint32, TSharedPtr<MServerConnection>>  ConnectionPool;
    TMap<EServerType, uint64>                    LastSeq;
    TSet<EServerType>                            PendingListTypes;
    // mutable:const 成员函数(GetServiceInstance(SubId))也需要加锁
    mutable std::mutex Mutex;
    MNetEventLoop*     EventLoop       = nullptr; // 非拥有
    MObject*           ServiceInstance = nullptr; // P2:非拥有,进程生命周期

    // 多 Reactor 扩展(P5):per-Sub 反射上下文 + per-Sub 连接池。启动期注入,运行期只读。
    // value 用 shared_ptr 保证 map 扩容后 SPerSubPool 地址仍稳定；全局 Mutex 只保护
    // PerSubPools / PerSubService 自身，池内容由各自 Mutex 保护。
    TMap<uint32, MObject*>                     PerSubService;
    TMap<uint32, TSharedPtr<SPerSubPool>> PerSubPools;
};
