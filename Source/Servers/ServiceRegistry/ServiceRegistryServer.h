/**
 * @file ServiceRegistryServer.h
 * @brief MServiceRegistry —— 服务注册中心:收集各 Service 端点、广播端点变更、剔除失联服务。
 *
 * @detail 每个入站连接对应一个 FRegistryClientSession。客户端先 Register 拿到 ServerId,
 *   之后周期 Heartbeat 续命、Deregister 下线、UpdateActors 上报本进程 actor 列表,
 *   ListEndpoints 拉取某类型的全量端点。端点表任何变更(注册/注销/上下线/actor 变更)都会触发
 *   SendEndpointChange,把该 ServerType 的全量端点以单调递增 Seq 广播给所有会话 —— 对端据此
 *   维护自己的路由表。
 *
 * @detail 线程模型:所有回调都跑在主线程(MNetServerBase 保证),Sessions / Endpoints 无需加锁。
 */
#pragma once

#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServiceDiscovery/Endpoint.h"
#include "Common/Net/ServiceDiscovery/RegistryProtocol.h"
#include "Common/Runtime/Object/IDisposable.h"
#include "Common/Runtime/Object/Object.h"
#include "Servers/ServiceRegistry/ServiceRegistryConfig.h"

/**
 * @brief FRegistryClientSession - 一个注册中心客户端连接的会话状态。
 *
 * @detail 由 OnAccept 创建;bRegistered 为 true 后 ServerId 才有意义(Register 成功才置位)。
 *   LastSeenMs 是最近收到该连接任意包的时间(毫秒),供 TickHeartbeats 判活。
 */
class FRegistryClientSession : public TEnableSharedFromThis<FRegistryClientSession> {
    public:
    uint64                     ConnId = 0;
    TSharedPtr<INetConnection> Conn;
    TByteArray                 RecvBuffer;
    uint32                     ServerId    = 0; // Register 成功前为 0
    bool                       bRegistered = false;
    uint64                     LastSeenMs  = 0;
};

/**
 * @brief MServiceRegistry - 服务注册中心进程(端点台账 + 变更广播 + 失联剔除)。
 *
 * @detail 生命周期由 MServiceMain::Run<MServiceRegistry, SServiceRegistryConfig> 驱动;
 *   Dispose() 幂等(配合 IDisposable),退出时关连接、清空会话表与端点表。
 */
MCLASS(Type = Service)
class MServiceRegistry : public MNetServerBase, public MObject, public IDisposable {
    public:
    MGENERATED_BODY(MServiceRegistry, MObject, 0)
    public:
    using MObject::Tick;

    bool   Init(int InPort = 0);
    void   Tick();
    void   TickBackends() override;
    void   ShutdownConnections() override;
    void   OnRunStarted() override;
    uint16 GetListenPort() const override;
    void   OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;

    void Dispose() override;

    private:
    /** @brief HandlePacket - 按首字节消息类型分发到各 Handle* 处理函数。
     *  @param Packet 已剥掉外层 length-prefix 的包体(首字节 = 消息类型,其后为 payload) */
    void HandlePacket(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Packet);

    /** @brief HandleRegister - 登记端点;ServerId 已被别的活跃会话占用时报 AlreadyExists。
     *  @param Payload 序列化的 FServiceEndpoint */
    void HandleRegister(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload);

    /** @brief HandleDeregister - 主动下线:移除端点并广播变更;ServerId 未知时报 NotFound。 */
    void HandleDeregister(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload);

    /** @brief HandleHeartbeat - 续命:刷新 LastHeartbeatMs;从不健康恢复时广播变更。
     *  @detail 包里的 ServerId 必须与该会话已注册的 ServerId 一致,否则回 server_id_mismatch。 */
    void HandleHeartbeat(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload);

    /** @brief HandleUpdateActors - 全量替换该端点的 ActorIds 并广播变更。 */
    void HandleUpdateActors(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload);

    /** @brief HandleListEndpoints - 拉取某 ServerType 的全部健康端点,按当前 Seq 打包单发回该会话。 */
    void HandleListEndpoints(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Payload);

    /** @brief SendTo - 把已构造好的包发给会话;会话为空或已断开时静默丢弃(不报错)。 */
    void SendTo(TSharedPtr<FRegistryClientSession> Session, const TByteArray& Packet);

    /** @brief SendAck - 回一个 Ack(状态码 + 可读文本)。 */
    void SendAck(TSharedPtr<FRegistryClientSession> Session, EServiceRegistryResult Status, const MString& Message);

    /** @brief SendEndpointChange - 收集某 ServerType 的全量端点(不过滤健康状态),用自增 Seq 广播给所有会话。 */
    void SendEndpointChange(EServerType ServerType);

    /** @brief TickHeartbeats - 遍历端点表:超时标不健康、更久则移除,最后对受影响的类型广播变更。 */
    void TickHeartbeats();

    SServiceRegistryConfig Config;

    // 端点表:key = ServerId。
    TMap<uint32, FServiceEndpoint> Endpoints;

    // 每个 ServerType 一份单调递增的端点快照序号,每次广播 +1;对端可据此判断"哪一版更新"。
    TMap<EServerType, uint64> MonotonicSeq;

    // 会话表:key = ConnId(注意:与端点表不同,未注册成功的会话也在表里)。
    TMap<uint64, TSharedPtr<FRegistryClientSession>> Sessions;

    // NOTE: NextSeq 从未被读写(序号实际来自 MonotonicSeq[ServerType]),疑似早期设计残留;
    //       此处保留不动,仅作记录。
    uint64 NextSeq = 0;
};