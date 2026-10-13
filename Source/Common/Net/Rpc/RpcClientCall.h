#pragma once

/**
 * @file RpcClientCall.h
 * @brief RpcClientCall - server→client 下行推送的两个薄封装。
 *
 * @detail 旧版的 SClientCallContext / GetCurrentClientConnectionId / CaptureCurrentClientCallContext /
 *   RegisterDeferredClientCall / BuildClientFunctionArgsPayload / BuildClientFunctionCallPacket* /
 *   FindClientDownlinkFunction* 全部已随 envelope 重构删除 —— 它们依赖的
 *   IClientResponseTarget / MClientDownlink / MessageType dispatch 整条路径已拆掉。
 *
 *   现在本文件只剩两种能力:
 *     - SendClientDownlink: 向单个连接发一条下行 (FunctionId, Payload)。
 *     - CallClient:         按 MClientTargetResolver 解析出的连接集合逐个调用前者。
 *
 *   wire 形态由 RpcTransport.cpp::BuildClientEnvelopePacket 定义
 *   ([RequestId:8B][FunctionId:2B][PayloadSize:4B][Payload:N]),纯反射,不带 MessageType 字节;
 *   下行统一写 RequestId = 0,与客户端自身的 Request/Response 区分。
 */

#include "Common/IO/Socket/Socket.h"
#include "Common/Net/ClientCall/MClientTargetResolver.h"
#include "Common/Net/Rpc/RpcTransport.h"
#include "Common/Runtime/MLib.h"

#include <cstdint>

namespace RpcClientCall {
    /**
     * @brief SendClientDownlink - 向单个连接发送一条反射下行推送。
     * @param Conn       目标连接;为空或未连接时直接失败
     * @param FunctionId 客户端侧反射函数 id,必须非 0
     * @param Payload    已反射序列化的参数负载
     * @return true 包已写入连接发送缓冲;false 连接不可用 / FunctionId==0 / 发送缓冲满
     *
     * @detail RequestId 固定写 0,客户端据此把该包判定为服务端主动推送(而非常规请求/响应)。
     */
    inline bool SendClientDownlink(TSharedPtr<INetConnection> Conn, uint16 FunctionId, const TByteArray& Payload) {
        if (!Conn || !Conn->IsConnected())
            return false;
        TByteArray Packet;
        if (!BuildClientEnvelopePacket(FunctionId, /*RequestId=*/0, Payload, Packet))
            return false;
        return Conn->Send(Packet.data(), static_cast<uint32>(Packet.size()));
    }

    /**
     * @brief CallClient - 服务器→客户端广播/定向推送,fire-and-forget。
     * @param FunctionId 客户端侧反射函数 id
     * @param Payload    已反射序列化的参数负载
     * @param bBroadcast true  = 推给 MClientTargetResolver 中所有"已注册且仍连接"的客户端
     *                   false = 只推给当前绑定上下文(MClientTargetContextGuard 建立)解析到的连接
     * @return 实际成功入队的连接数;解析不到任何目标时为 0
     *
     * @detail 单个连接失败不会中断循环,也不计入返回值;需要严格投递保证的调用方要自行比对
     *   期望连接数。本函数不等待任何回包。
     */
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