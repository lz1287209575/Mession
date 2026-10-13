/**
 * @file GatewayServer.h
 * @brief MGatewayServer —— 客户端接入网关:收客户端 envelope、按 manifest 路由到业务服务、回推下行。
 *
 * @detail Gateway 自身零业务协议:入站客户端包按 MClientManifest 查 FunctionId,用条目里的
 *   TargetName 选目标服务进程(Player / Echo ...),再把原始 Payload 经 ServerCall 转发过去;
 *   目标进程的响应经注册的 ServerCall 回调原样回给客户端。下行(服务端 -> 客户端)由业务服务
 *   经 PushClientDownlink 发起,Gateway 按连接 id 定向投递、或 GatewayConnectionId==0 广播。
 */
#pragma once

#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Common/ClientDownlinkMessages.h"
#include "Protocol/Messages/Common/ControlPlaneMessages.h"
#include "Protocol/Messages/EchoService/FSampleEchoMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/App/ServiceMain.h"

class MGatewayServer;
extern MGatewayServer* GGlobalGateway;

/** @brief SGatewayConfig - MGatewayServer 的 CLI 配置(MSTRUCT + MPROPERTY,由反射解析)。 */
MSTRUCT()
struct SGatewayConfig {
    MPROPERTY(Meta = (Cli = "--listen"))
    uint16 ListenPort = 8001;

    // Service 注册中心地址（"127.0.0.1:18000"）。PoC 阶段强制要求传
    // ——MEndpointCache 启动期就要连 Registry 取全量 endpoint。
    MPROPERTY(Meta = (Cli = "--registry"))
    MString RegistryAddr;

    // PoC 阶段：Gateway 没有 LocalActorIds / LocalInstId（不做业务），
    // 但 MakeLocalEndpoint 模板需要这些字段——这里给个空的默认值让
    // 模板实例化能通过。运行时 Gateway 通过 MEndpointCache 寻址 EchoService。
    MPROPERTY()
    EServerType LocalServerType = EServerType::Unknown;

    MPROPERTY()
    uint32 LocalServerId = 0;

    MPROPERTY()
    TVector<uint32> LocalActorIds;

    // 日志：ServiceMain::Run 会读这些字段初始化 MLog 管道。
    MPROPERTY(Meta = (Cli = "--log-file"))
    MString LogFilePath = "";

    MPROPERTY(Meta = (Cli = "--log-rotate-bytes"))
    uint64 LogRotateBytes = 100ull * 1024ull * 1024ull;

    MPROPERTY(Meta = (Cli = "--log-archives"))
    uint32 LogArchives = 5;

    MPROPERTY(Meta = (Cli = "--log-config"))
    MString LogConfigPath = "";
};

/**
 * @brief MGatewayServer - 客户端网关服务(ServerCall 路由 + 下行推送)。
 *
 * @detail 生命周期由 MServiceMain::Run<MGatewayServer, SGatewayConfig> 驱动。GGlobalGateway
 *   是本进程单例。线程模型:OnAccept 与所有业务回调都在主线程(MNetServerBase 保证),
 *   因此 ClientConnections 不需要加锁。
 */
MCLASS(Type = Server)
class MGatewayServer : public MNetServerBase, public MObject {
    public:
    MGENERATED_BODY(MGatewayServer, MObject, 0)
    public:
    using MObject::Tick;

    bool Init(int InPort = 0);
    void Tick();
    void Run() override {
        MNetServerBase::Run();
    }

    uint16 GetListenPort() const override;
    void   OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void   TickBackends() override;
    void   ShutdownConnections() override;
    void   OnRunStarted() override;

    /**
     * @brief PushClientDownlink - 下行推送:把 envelope 发给指定客户端连接,或广播给全部客户端。
     * @param Request.FunctionId 下行函数 id(必填;为 0 直接返回错误)
     * @param Request.GatewayConnectionId 目标客户端连接 id;0 = 广播
     * @param Request.Payload 业务负载
     * @return 空 Ok;失败时返回 Err,码为 client_downlink_function_required /
     *         client_downlink_packet_build_failed / gateway_client_connection_missing /
     *         client_downlink_send_failed
     *
     * @detail 是 EchoService 等业务服务下发通知的统一入口。envelope 里的 RequestId 固定写 0,
     *   表示"服务端主动 push"而非对某次请求的响应。
     */
    MFUNCTION(ServerCall)
    MFuture<TResult<SEmptyServerMessage, FAppError>> PushClientDownlink(const FClientDownlinkPushRequest& Request);

    /**
     * @brief Rpc_OnServerHandshake - 传输层握手桩:无状态 ack。
     * @param DummyServerId 占位参数(ServerCall stub 只支持单参数),实现里被忽略
     * @return 空 Ok 响应
     *
     * @detail 与 MEchoService::Rpc_OnServerHandshake 同源,由 MServerConnection::SendHandshake 触发。
     */
    MFUNCTION(ServerCall)
    MFuture<TResult<SEmptyServerMessage, FAppError>> Rpc_OnServerHandshake(uint32 DummyServerId);

    /** @brief Rpc_OnHeartbeat - 传输层心跳桩:无状态 ack。
     *  @param Seq 占位参数(同 Rpc_OnServerHandshake),实现里被忽略 */
    MFUNCTION(ServerCall)
    MFuture<TResult<SEmptyServerMessage, FAppError>> Rpc_OnHeartbeat(uint32 Seq);

    /** @brief GetSingleton - 返回本进程单例 GGlobalGateway(Init() 之前为 nullptr)。 */
    static MGatewayServer* GetSingleton() {
        return GGlobalGateway;
    }

    private:
    /**
     * @brief HandleClientPacket - 客户端入站包处理:解析 envelope -> 查 manifest -> 转发并回包。
     * @param ConnectionId 来源客户端连接
     * @param Data 已剥掉外层长度前缀的包体
     *
     * @detail 处理期间用 MClientTargetContextGuard 把当前连接绑定为推送目标,使本次调用中产生的
     *   CallClient 下行能解析到该客户端。
     *   失败行为:manifest 未命中、TargetName 未知、目标连接不可用、ServerCall 注册或发送失败时,
     *   回一个 Payload 为空的 envelope 当错误响应并 LOG_WARN;不抛异常、不中断进程。
     */
    void HandleClientPacket(uint64 ConnectionId, const TByteArray& Data);

    SGatewayConfig Config;

    // 客户端连接表(connId -> 连接)。注意:服务器间连接在 OnAccept 里也会被注册进来,
    // 因此广播下行可能也发给它们,但会被对端按非客户端包忽略,无害。
    TMap<uint64, TSharedPtr<INetConnection>> ClientConnections;
};
