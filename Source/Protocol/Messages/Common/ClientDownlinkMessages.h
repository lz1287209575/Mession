#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

// Client↔Gateway 下行推送请求。
//
//   - GatewayConnectionId: Gateway 内部 UE 连接 id,跟 MRpcChannel::SendToClient
//                          拿到的 ConnectionId 是同一张表(ClientConnections)。
//   - FunctionId: 由 MHeaderTool 在 MClientDownlinkManifest 算出来。
//   - Payload:   MSTRUCT 反射字节流(BroadcastPayload / FSampleEchoResponse / ...)。
//
// Gateway 收到这个请求后,用 GatewayConnectionId 查 ClientConnections 拿到
// 底层 TCP INetConnection,然后 BuildClientEnvelopePacket(FunctionId,
// RequestId /* Gateway 自管 correlation id */, Payload) 发出去。

MSTRUCT()
struct FClientDownlinkPushRequest
{
    MPROPERTY()
    uint64 GatewayConnectionId = 0;

    MPROPERTY()
    uint16 FunctionId = 0;

    MPROPERTY()
    TByteArray Payload;
};