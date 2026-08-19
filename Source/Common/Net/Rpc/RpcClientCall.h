#pragma once

// step-2: 原 RpcClientCall 整个文件基本被清空。
//
//   原 SClientCallContext / GetCurrentClientConnectionId / CaptureCurrentClientCallContext
//   / RegisterDeferredClientCall / BuildClientFunctionArgsPayload / BuildClientFunctionCallPacket*
//   / FindClientDownlinkFunction* 全部删除 —— 它们依赖的 IClientResponseTarget /
//   MClientDownlink / MessageType dispatch 整条路径已拆。
//
//   新 Client↔Gateway envelope 由 RpcTransport.cpp::BuildClientEnvelopePacket /
//   ParseClientEnvelopePacket 负责,纯反射(只看 FunctionId + Payload)。
//   server→client 下行（CallClient）：目标连接由 MClientTargetResolver 解析
//   （当前绑定上下文 / 广播），发送经 BuildClientEnvelopePacket（无 MessageType）。

#include "Common/IO/Socket/Socket.h"
#include "Common/Net/ClientCall/MClientTargetResolver.h"
#include "Common/Net/Rpc/RpcTransport.h"
#include "Common/Runtime/MLib.h"

#include <cstdint>

namespace RpcClientCall {
    // 向单个连接发送下行（FunctionId + Payload，RequestId=0 表示主动推送）。
    inline bool SendClientDownlink(TSharedPtr<INetConnection> Conn, uint16 FunctionId, const TByteArray& Payload) {
        if (!Conn || !Conn->IsConnected())
            return false;
        TByteArray Packet;
        if (!BuildClientEnvelopePacket(FunctionId, /*RequestId=*/0, Payload, Packet))
            return false;
        return Conn->Send(Packet.data(), static_cast<uint32>(Packet.size()));
    }

    // 服务器→客户端推送：bBroadcast=true 推给所有已注册连接，
    // 否则推给当前绑定上下文（MClientTargetContextGuard）解析到的连接。
    // 返回实际发送的连接数。
    inline size_t CallClient(uint16 FunctionId, const TByteArray& Payload, bool bBroadcast = false) {
        auto&                                     Resolver = MClientTargetResolver::Get();
        const TVector<TSharedPtr<INetConnection>> Targets  = bBroadcast ? Resolver.ResolveBroadcast() : Resolver.ResolveCurrentContext();
        size_t                                    Sent     = 0;
        for (const auto& Conn : Targets) {
            if (SendClientDownlink(Conn, FunctionId, Payload))
                ++Sent;
        }
        return Sent;
    }
} // namespace RpcClientCall