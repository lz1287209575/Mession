/**
 * @file ClientDownlinkMessages.h
 * @brief Client ↔ Gateway 下行推送的请求结构(业务服 → Gateway)。
 *
 * 服务端要主动给某个客户端推消息时并不直接持有客户端连接,而是把本请求发给
 * Gateway,由 Gateway 落成真正的下行封包:
 *
 * 1. 用 GatewayConnectionId 查 Gateway 内部的 ClientConnections 表,拿到底层 TCP
 *    INetConnection —— 该 id 与 MRpcChannel::SendToClient 拿到的 ConnectionId
 *    是同一张表的键;
 * 2. 用 BuildClientEnvelopePacket(FunctionId, RequestId, Payload) 组包发出,其中
 *    RequestId 由 Gateway 自己分配,只用于关联这一次下行。
 *
 * @note FunctionId 由 MHeaderTool 依据 MClientDownlinkManifest 算出,不是手写常量;
 *       收发两端必须用同一份 manifest 生成的 id 表。
 */
#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief FClientDownlinkPushRequest - 一次下行推送请求。
 */
MSTRUCT()
struct FClientDownlinkPushRequest {
    /// Gateway 内部的客户端连接 id(ClientConnections 表键)
    MPROPERTY()
    uint64 GatewayConnectionId = 0;

    /// 下行函数 id(MHeaderTool 依 MClientDownlinkManifest 生成)
    MPROPERTY()
    uint16 FunctionId = 0;

    /// MSTRUCT 反射字节流;具体类型由 FunctionId 决定
    /// (BroadcastPayload / FSampleEchoResponse / ...)
    MPROPERTY()
    TByteArray Payload;
};