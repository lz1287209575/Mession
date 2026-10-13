/**
 * @file Endpoint.h
 * @brief 服务发现 wire 类型 FServiceEndpoint,以及 Registry 协议的类型枚举与编解码入口。
 *
 * @detail 本文件是多方共享的契约点:
 *   - FServiceEndpoint 既用于 Registry 的注册/推送包,又带 MPROPERTY 反射序列化,
 *     因此字段名/顺序/类型是公开 ABI(禁止改名、重排、增删);
 *   - EServiceRegistryMessageType 的取值即包首第 1 字节,供接收方分发;
 *   - 具体编解码是手写的顺序布局,实现见 Endpoint.cpp。
 */
#pragma once

#include "Common/Net/ServerConnection.h" // EServerType
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Servers/App/ServiceId.h"

/**
 * @brief FServiceEndpoint - 一个服务实例的可发现信息。
 *
 * @detail 字段顺序即 wire 布局(EncodeEndpoint / DecodeEndpoint 严格按声明顺序读写):
 *   ServerType + ServerId 定位实例;Address/Port 给出连接目标;
 *   LastHeartbeatMs 是 Registry 维护的心跳时间戳(毫秒);
 *   bHealthy 为 false 时路由不会选中该实例;
 *   ActorIds 是该实例承载的 actor 列表(64 位 ActorId,布局见 MServiceId)。
 * PoC 阶段 Address 恒为 "0.0.0.0"(Registry 不解析 IP,真正连接由对端按 loopback 处理)。
 */
MSTRUCT()
struct FServiceEndpoint {
    MPROPERTY() EServerType ServerType = EServerType::Unknown;
    MPROPERTY() uint32 ServerId        = 0;
    MPROPERTY() MString Address        = "0.0.0.0";
    MPROPERTY() uint16 Port            = 0;
    MPROPERTY() uint64 LastHeartbeatMs = 0;
    MPROPERTY() bool bHealthy          = true;
    MPROPERTY() TVector<uint64> ActorIds;
};

/**
 * @brief EServiceRegistryMessageType - Registry 协议的包类型(包首 1 字节)。
 *
 * @detail 刻意独立于 EServerMessageType:裸 uint8 包类型会被直接交给
 * MServerConnection 分发,不能与服务器间消息号撞车(MT_RPC=27 / MT_FunctionCall=28 /
 * MT_FunctionResponse=29,见 Common/Net/ServerConnection.h 的 EServerMessageType)。
 * 基数取 200,与上述取值留足余量。
 * Client↔Gateway 的信封由 RpcTransport.cpp::BuildClientEnvelopePacket 编解码,
 * 与本枚举无关(2026-07 step-1/2 协议族重构后 EClientMessageType 整个 enum 已删除)。
 */
enum class EServiceRegistryMessageType : uint8_t {
    Register       = 200, // 注册本 Service 实例
    Deregister     = 201, // 注销
    Heartbeat      = 202, // 心跳(保活 + 刷新 LastHeartbeatMs)
    UpdateActors   = 203, // 全量更新本实例承载的 actor 列表
    ListEndpoints  = 204, // 主动拉取某 ServerType 的端点快照
    EndpointChange = 205, // Registry 推送端点变化(带单调递增 Seq)
    Ack            = 206, // 带状态码的应答
};

/**
 * @brief EServiceRegistryResult - Registry 应答(Ack)的状态码。
 *
 * @detail Ok = 成功;AlreadyExists = ServerId 重复注册;NotFound = 目标不存在;
 * InvalidPayload = 包体解析失败;StaleSeq = Seq 过期被拒。
 */
enum class EServiceRegistryResult : uint8_t {
    Ok             = 0,
    AlreadyExists  = 1,
    NotFound       = 2,
    InvalidPayload = 3,
    StaleSeq       = 4,
};

/**
 * @brief EncodeEndpoint - 按固定顺序把端点追加写入 Out。
 * @param Ep  待编码端点
 * @param Out 输出缓冲(会先被 clear)
 * @return 恒为 true:当前实现没有任何失败路径
 */
bool EncodeEndpoint(const FServiceEndpoint& Ep, TByteArray& Out);

/**
 * @brief DecodeEndpoint - 从 In 的 Off 处解码一个端点并推进 Off。
 * @param In  输入字节流
 * @param Off 读写游标(字节);成功时前进,失败时可能已部分前进,调用方应整体丢弃该缓冲
 * @param Ep  输出端点(ActorIds 先被清空再填充)
 * @return true 成功;false 数据不足
 */
bool DecodeEndpoint(const TByteArray& In, size_t& Off, FServiceEndpoint& Ep);

/**
 * @brief MakeLocalEndpoint - 把本进程的 Service 配置转成 FServiceEndpoint(供 Registry 注册)。
 * @param Config 需提供 LocalServerType / LocalServerId / ListenPort / LocalActorIds 字段
 * @return 填好的端点;LastHeartbeatMs 与 bHealthy 保持默认值,由 Registry 侧维护
 *
 * @detail Address 恒为 "0.0.0.0":PoC 阶段 Registry 不解析 IP,真正连谁由 Service 自己在
 * MEndpointCache::LazyConnect 里用 loopback + Config.ListenPort 决定(跨主机方案见后续 spec)。
 * Config.LocalActorIds 里是 CLI 形式的 InstId(低 32 位),这里换成 64 位 ActorId
 * (高 32 位 = ServiceId = EServerType 数值,见 MServiceId::Make)。
 */
template <typename TConfig> FServiceEndpoint MakeLocalEndpoint(const TConfig& Config) {
    FServiceEndpoint Ep;
    Ep.ServerType = Config.LocalServerType;
    Ep.ServerId   = Config.LocalServerId;
    Ep.Address    = "0.0.0.0";
    Ep.Port       = Config.ListenPort;
    Ep.ActorIds.reserve(Config.LocalActorIds.size());
    for (uint32 InstId : Config.LocalActorIds) {
        Ep.ActorIds.push_back(MServiceId::Make(Config.LocalServerType, InstId));
    }
    return Ep;
}

/**
 * @brief ParseAddrPort - 把 "host:port" 拆成 host + port。
 * @param In      形如 "127.0.0.1:18000" 的字符串
 * @param OutAddr 输出:冒号之前的 host
 * @param OutPort 输出:端口,合法范围 1..65535
 * @return true 解析成功;false 格式错误(没有冒号、host 为空、端口为空/非正/越界)
 *
 * @detail 取的是 rfind,即以最后一个冒号分隔,因此不支持 IPv6 字面量。
 * 端口用 std::stoi 转换:它只解析前缀数字,尾随垃圾会被静默忽略
 * (例:"127.0.0.1:18000abc" 也能通过并得到 18000)。
 * 实际调用方是各 Service 启动时解析 CLI 参数 --registry=host:port。
 * 失败路径只返回 false,不改写 OutAddr/OutPort。
 */
inline bool ParseAddrPort(const MString& In, MString& OutAddr, uint16& OutPort) {
    const size_t ColonPos = In.rfind(':');
    if (ColonPos == MString::npos || ColonPos == 0 || ColonPos + 1 >= In.size()) {
        return false;
    }
    const MString PortStr = In.substr(ColonPos + 1);
    int           PortInt = 0;
    try {
        PortInt = std::stoi(PortStr.c_str());
    } catch (const std::exception&) {
        return false;
    }
    if (PortInt <= 0 || PortInt > 65535) {
        return false;
    }
    OutAddr = In.substr(0, ColonPos);
    OutPort = static_cast<uint16>(PortInt);
    return true;
}
