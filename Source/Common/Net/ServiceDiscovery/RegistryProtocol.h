/**
 * @file RegistryProtocol.h
 * @brief Registry 协议(服务发现控制面)的包组装与解析。
 *
 * @detail 统一格式:每个 Build* 的输出都以 1 字节 EServiceRegistryMessageType 开头,
 * 可以直接交给 MTcpConnection::Send(它再用 MLengthPrefixedPacketCodec 补长度前缀)。
 * 对应的 Parse* 只吃"已经剥掉那 1 字节 PacketType"之后的 payload:读第 1 字节并分发的
 * 工作由调用方完成(Registry 服务端 / MEndpointCache::HandleRegistryPacket)。
 * 所有 Build* 当前恒返回 true(无失败路径);Parse* 返回 false 表示包体不合法。
 */
#pragma once

#include "Common/Net/ServiceDiscovery/Endpoint.h"

namespace RegistryProtocol {
    /**
     * @brief BuildRegistryRegisterPacket - 组装注册包(Register)。
     * @param Endpoint  本实例的端点信息(见 FServiceEndpoint)
     * @param OutPacket 输出:1 字节类型 + EncodeEndpoint 的端点体
     * @return true(当前无失败路径);EncodeEndpoint 失败会返回 false
     */
    bool BuildRegistryRegisterPacket(const FServiceEndpoint& Endpoint, TByteArray& OutPacket);

    /**
     * @brief BuildRegistryDeregisterPacket - 组装注销包(Deregister)。
     * @param ServerId  要注销的 ServerId
     * @param OutPacket 输出
     */
    bool BuildRegistryDeregisterPacket(uint32 ServerId, TByteArray& OutPacket);

    /**
     * @brief BuildRegistryHeartbeatPacket - 组装心跳包(Heartbeat)。
     * @param ServerId   本实例 ServerId
     * @param TimestampMs 发送时刻(毫秒),Registry 用它刷新 LastHeartbeatMs
     * @param OutPacket  输出
     */
    bool BuildRegistryHeartbeatPacket(uint32 ServerId, uint64 TimestampMs, TByteArray& OutPacket);

    /**
     * @brief BuildRegistryUpdateActorsPacket - 组装"全量更新 actor 列表"包(UpdateActors)。
     * @param ServerId  本实例 ServerId
     * @param ActorIds  新的完整 actor 列表(全量语义,非增量);个数被截断为 16 位
     * @param OutPacket 输出
     */
    bool BuildRegistryUpdateActorsPacket(uint32 ServerId, const TVector<uint64>& ActorIds, TByteArray& OutPacket);

    /**
     * @brief BuildRegistryListEndpointsPacket - 组装"拉取端点快照"包(ListEndpoints)。
     * @param ServerType 想拉取的业务类型
     * @param OutPacket  输出
     */
    bool BuildRegistryListEndpointsPacket(EServerType ServerType, TByteArray& OutPacket);

    /**
     * @brief BuildRegistryEndpointChangePacket - 组装端点变化推送(EndpointChange)。
     * @param ServerType 本次推送针对的业务类型
     * @param Seq        单调递增序号,接收方据此拒绝过期推送
     * @param Endpoints  该类型的完整端点快照(全量,非增量)
     * @param OutPacket  输出;每个端点各自带 2 字节长度前缀以便前向扩展
     * @return true(当前无失败路径);EncodeEndpoint 失败会返回 false
     */
    bool BuildRegistryEndpointChangePacket(EServerType ServerType, uint64 Seq, const TVector<FServiceEndpoint>& Endpoints, TByteArray& OutPacket);

    /**
     * @brief BuildRegistryAckPacket - 组装应答包(Ack)。
     * @param Status   应答状态码
     * @param Message  可读信息(2 字节长度 + 裸字节,超 65535 会被截断)
     * @param OutPacket 输出
     */
    bool BuildRegistryAckPacket(EServiceRegistryResult Status, const MString& Message, TByteArray& OutPacket);

    // ---- 解析:入参是已剥掉 PacketType 的 payload ----
    /**
     * @brief ParseRegistryRegisterPacket - 解析注册包体。
     * @return true 成功且恰好消费完整个 payload(尾部有多余字节算失败)
     */
    bool ParseRegistryRegisterPacket(const TByteArray& Payload, FServiceEndpoint& OutEndpoint);

    /**
     * @brief ParseRegistryDeregisterPacket - 解析注销包体(4 字节 ServerId)。
     */
    bool ParseRegistryDeregisterPacket(const TByteArray& Payload, uint32& OutServerId);

    /**
     * @brief ParseRegistryHeartbeatPacket - 解析心跳包体(4 字节 ServerId + 8 字节时间戳)。
     */
    bool ParseRegistryHeartbeatPacket(const TByteArray& Payload, uint32& OutServerId, uint64& OutTimestampMs);

    /**
     * @brief ParseRegistryUpdateActorsPacket - 解析 actor 全量更新包体。
     * @param OutActorIds 输出:被清空后重新填充
     */
    bool ParseRegistryUpdateActorsPacket(const TByteArray& Payload, uint32& OutServerId, TVector<uint64>& OutActorIds);

    /**
     * @brief ParseRegistryListEndpointsPacket - 解析拉取请求包体(4 字节 ServerType)。
     */
    bool ParseRegistryListEndpointsPacket(const TByteArray& Payload, EServerType& OutServerType);

    /**
     * @brief ParseRegistryEndpointChangePacket - 解析端点变化推送包体。
     * @param OutEndpoints 输出:被清空后重新填充
     */
    bool ParseRegistryEndpointChangePacket(const TByteArray& Payload, EServerType& OutServerType, uint64& OutSeq, TVector<FServiceEndpoint>& OutEndpoints);

    /**
     * @brief ParseRegistryAckPacket - 解析应答包体(1 字节状态码 + 字符串)。
     */
    bool ParseRegistryAckPacket(const TByteArray& Payload, EServiceRegistryResult& OutStatus, MString& OutMessage);
} // namespace RegistryProtocol
