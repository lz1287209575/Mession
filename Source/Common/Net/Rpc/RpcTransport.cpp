/**
 * @file RpcTransport.cpp
 * @brief RpcTransport 的实现:三类报文的拼包/解包与发送。
 *
 * @detail 全部整数走 memcpy / AppendValue(主机字节序),不做网络序转换 —— 只在
 *   little-endian 同架构集群内互通。所有 Parse* 都会先校验 header 完整、
 *   再校验声明长度不超过实际数据,失败一律返回 false,不抛异常、不越界读。
 */

#include "Common/Net/Rpc/RpcTransport.h"

#include <cstring>

/**
 * @brief 拼 [FunctionId:2B][PayloadSize:4B][Payload:N]。恒返回 true(不校验 FunctionId)。
 */
bool BuildServerRpcPayload(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutData) {
    const uint32 PayloadSize = static_cast<uint32>(InPayload.size());

    OutData.clear();
    OutData.reserve(sizeof(FunctionId) + sizeof(PayloadSize) + PayloadSize);

    const uint8* FuncPtr = reinterpret_cast<const uint8*>(&FunctionId);
    OutData.insert(OutData.end(), FuncPtr, FuncPtr + sizeof(FunctionId));

    const uint8* SizePtr = reinterpret_cast<const uint8*>(&PayloadSize);
    OutData.insert(OutData.end(), SizePtr, SizePtr + sizeof(PayloadSize));

    if (PayloadSize > 0) {
        OutData.insert(OutData.end(), InPayload.begin(), InPayload.end());
    }

    return true;
}

/**
 * @brief 在负载前补 1 字节 MT_RPC。恒返回 true。
 */
bool BuildServerRpcMessage(const TByteArray& RpcPayload, TByteArray& OutPacket) {
    OutPacket.clear();
    OutPacket.reserve(1 + RpcPayload.size());
    OutPacket.push_back(static_cast<uint8>(EServerMessageType::MT_RPC));
    OutPacket.insert(OutPacket.end(), RpcPayload.begin(), RpcPayload.end());
    return true;
}

/// @brief 走 MServerConnection::SendPacket(由它补 MT_RPC 类型字节)。
bool SendServerRpcMessage(MServerConnection& Connection, const TByteArray& RpcPayload) {
    return Connection.SendPacket(static_cast<uint8>(EServerMessageType::MT_RPC), RpcPayload.empty() ? nullptr : RpcPayload.data(), static_cast<uint32>(RpcPayload.size()));
}

/// @brief shared_ptr 重载;Connection 为空返回 false。
bool SendServerRpcMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& RpcPayload) {
    return Connection ? SendServerRpcMessage(*Connection, RpcPayload) : false;
}

/// @brief 通用连接版:先 BuildServerRpcMessage 再 Send(多一次整包拷贝)。
bool SendServerRpcMessage(INetConnection& Connection, const TByteArray& RpcPayload) {
    TByteArray Packet;
    BuildServerRpcMessage(RpcPayload, Packet);
    return Connection.Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

/// @brief shared_ptr 重载;Connection 为空返回 false。
bool SendServerRpcMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& RpcPayload) {
    return Connection ? SendServerRpcMessage(*Connection, RpcPayload) : false;
}

// Client↔Gateway 反射 envelope:不带 MessageType 字节。
//
// wire 形态(整包长度前缀由 MLengthPrefixedPacketCodec 处理,不在本函数范围内):
//   [RequestId: 8B][FunctionId: 2B][PayloadSize: 4B][Payload: N]
//
// RequestId 由 UE 客户端唯一分配,Gateway 只透传不解释:
//   - 普通上行请求:客户端给一个 id,Gateway 回包时原样带回同一个 id;
//   - 服务端主动 push(下行):固定写 0(见 MGatewayServer::PushClientDownlink 与
//     RpcClientCall::SendClientDownlink),以此与 RequestId != 0 的请求/响应区分。
//
// "Request/Response/Push" 的区分完全交给 RequestId + 客户端自己的 manifest,
// Gateway 不做 enum 分发(架构决定)。

/**
 * @brief 拼反射 envelope。FunctionId == 0 时返回 false(信封无类型字节,0 无法表达有效包)。
 */
bool BuildClientEnvelopePacket(uint16 FunctionId, uint64 RequestId, const TByteArray& InPayload, TByteArray& OutPacket) {
    if (FunctionId == 0) {
        return false;
    }

    OutPacket.clear();
    OutPacket.reserve(sizeof(RequestId) + sizeof(FunctionId) + sizeof(uint32) + InPayload.size());

    const uint8* ReqPtr = reinterpret_cast<const uint8*>(&RequestId);
    OutPacket.insert(OutPacket.end(), ReqPtr, ReqPtr + sizeof(RequestId));

    const uint8* FuncPtr = reinterpret_cast<const uint8*>(&FunctionId);
    OutPacket.insert(OutPacket.end(), FuncPtr, FuncPtr + sizeof(FunctionId));

    AppendValue(OutPacket, static_cast<uint32>(InPayload.size()));
    OutPacket.insert(OutPacket.end(), InPayload.begin(), InPayload.end());
    return true;
}

/**
 * @brief 解反射 envelope 头(14 字节),并校验负载完整。
 * @return true 头完整且 Data 容得下声明的负载;否则 false(调用方应丢包)
 */
bool ParseClientEnvelopePacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutRequestId, uint32& OutPayloadSize, size_t& OutPayloadOffset) {
    const size_t HeaderSize = sizeof(uint64) + sizeof(uint16) + sizeof(uint32);
    if (Data.size() < HeaderSize) {
        return false;
    }

    std::memcpy(&OutRequestId, Data.data(), sizeof(OutRequestId));
    std::memcpy(&OutFunctionId, Data.data() + sizeof(uint64), sizeof(OutFunctionId));
    std::memcpy(&OutPayloadSize, Data.data() + sizeof(uint64) + sizeof(uint16), sizeof(OutPayloadSize));
    OutPayloadOffset = HeaderSize;
    return Data.size() >= OutPayloadOffset + static_cast<size_t>(OutPayloadSize);
}

// ---------------------------------------------------------------------------
// 已废弃的空壳(4 个)。旧版 Client↔Gateway 报文在 FunctionId 之前附 1 字节
// MessageType 头(旧注释写作 MT_FunctionCall=13,该数字是错的:枚举里
// EServerMessageType::MT_FunctionCall == 28)。Gateway 已统一切到上面的
// BuildClientEnvelopePacket / ParseClientEnvelopePacket,仓库内已无调用点。
// 保留它们只是为避免仍在引用这些符号的外部 stub 编译失败,行为约定:
// 清空输出参数并返回 false。
// ---------------------------------------------------------------------------

/// @brief 已废弃空壳:清空 OutPacket 并返回 false。
bool BuildClientFunctionPacket(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutPacket) {
    // 已废弃:旧实现(带 1 字节 MessageType 头)整体删除,统一用 BuildClientEnvelopePacket。
    // 保留空实现是为了兼容仍在引用该符号的 stub。
    (void)FunctionId;
    (void)InPayload;
    OutPacket.clear();
    return false;
}

/// @brief 已废弃空壳:清空 OutPacket 并返回 false。
bool BuildClientCallPacket(uint16 FunctionId, uint64 CallId, const TByteArray& InPayload, TByteArray& OutPacket) {
    // 已废弃:旧实现整体删除(Client↔Gateway 已无 CallId 概念)。
    (void)FunctionId;
    (void)CallId;
    (void)InPayload;
    OutPacket.clear();
    return false;
}

/// @brief 已废弃空壳:返回 false(输出参数不被写)。
bool ParseClientFunctionPacket(const TByteArray& Data, uint16& OutFunctionId, uint32& OutPayloadSize, size_t& OutPayloadOffset) {
    // 已废弃:Gateway 已切到 ParseClientEnvelopePacket,这里仅保留签名。
    (void)Data;
    (void)OutFunctionId;
    (void)OutPayloadSize;
    (void)OutPayloadOffset;
    return false;
}

/// @brief 已废弃空壳:返回 false(输出参数不被写)。
bool ParseClientCallPacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, uint32& OutPayloadSize, size_t& OutPayloadOffset) {
    // 已废弃:旧实现整体删除。
    (void)Data;
    (void)OutFunctionId;
    (void)OutCallId;
    (void)OutPayloadSize;
    (void)OutPayloadOffset;
    return false;
}

/// @brief 拼 ServerCall 请求包 [FunctionId:2B][CallId:8B][PayloadSize:4B][Payload:N];id 为 0 时失败。
bool BuildServerCallPacket(uint16 FunctionId, uint64 CallId, const TByteArray& InPayload, TByteArray& OutPayload) {
    if (FunctionId == 0 || CallId == 0) {
        return false;
    }

    OutPayload.clear();
    OutPayload.reserve(sizeof(FunctionId) + sizeof(CallId) + sizeof(uint32) + InPayload.size());
    AppendValue(OutPayload, FunctionId);
    AppendValue(OutPayload, CallId);
    AppendValue(OutPayload, static_cast<uint32>(InPayload.size()));
    OutPayload.insert(OutPayload.end(), InPayload.begin(), InPayload.end());
    return true;
}

/// @brief 拼 ServerCall 回包 [FunctionId:2B][CallId:8B][Success:1B][PayloadSize:4B][Payload:N];id 为 0 时失败。
bool BuildServerCallResponsePacket(uint16 FunctionId, uint64 CallId, bool bSuccess, const TByteArray& InPayload, TByteArray& OutPayload) {
    if (FunctionId == 0 || CallId == 0) {
        return false;
    }

    OutPayload.clear();
    OutPayload.reserve(sizeof(FunctionId) + sizeof(CallId) + sizeof(uint8) + sizeof(uint32) + InPayload.size());
    AppendValue(OutPayload, FunctionId);
    AppendValue(OutPayload, CallId);
    AppendValue(OutPayload, static_cast<uint8>(bSuccess ? 1 : 0));
    AppendValue(OutPayload, static_cast<uint32>(InPayload.size()));
    OutPayload.insert(OutPayload.end(), InPayload.begin(), InPayload.end());
    return true;
}

/// @brief 解 ServerCall 请求包头(14 字节)并校验负载完整。
bool ParseServerCallPacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, uint32& OutPayloadSize, size_t& OutPayloadOffset) {
    const size_t HeaderSize = sizeof(uint16) + sizeof(uint64) + sizeof(uint32);
    if (Data.size() < HeaderSize) {
        return false;
    }

    size_t Offset = 0;
    std::memcpy(&OutFunctionId, Data.data() + Offset, sizeof(OutFunctionId));
    Offset += sizeof(OutFunctionId);
    std::memcpy(&OutCallId, Data.data() + Offset, sizeof(OutCallId));
    Offset += sizeof(OutCallId);
    std::memcpy(&OutPayloadSize, Data.data() + Offset, sizeof(OutPayloadSize));
    Offset += sizeof(OutPayloadSize);
    OutPayloadOffset = Offset;
    return Data.size() >= OutPayloadOffset + static_cast<size_t>(OutPayloadSize);
}

/// @brief 解 ServerCall 回包头(15 字节)并校验负载完整。
bool ParseServerCallResponsePacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, bool& OutSuccess, uint32& OutPayloadSize, size_t& OutPayloadOffset) {
    const size_t HeaderSize = sizeof(uint16) + sizeof(uint64) + sizeof(uint8) + sizeof(uint32);
    if (Data.size() < HeaderSize) {
        return false;
    }

    size_t Offset      = 0;
    uint8  SuccessByte = 0;
    std::memcpy(&OutFunctionId, Data.data() + Offset, sizeof(OutFunctionId));
    Offset += sizeof(OutFunctionId);
    std::memcpy(&OutCallId, Data.data() + Offset, sizeof(OutCallId));
    Offset += sizeof(OutCallId);
    std::memcpy(&SuccessByte, Data.data() + Offset, sizeof(SuccessByte));
    Offset += sizeof(SuccessByte);
    std::memcpy(&OutPayloadSize, Data.data() + Offset, sizeof(OutPayloadSize));
    Offset += sizeof(OutPayloadSize);
    OutSuccess       = SuccessByte != 0;
    OutPayloadOffset = Offset;
    return Data.size() >= OutPayloadOffset + static_cast<size_t>(OutPayloadSize);
}

/// @brief 走 MServerConnection::SendPacket(由它补 MT_FunctionCall 类型字节)。
bool SendServerCallMessage(MServerConnection& Connection, const TByteArray& PacketPayload) {
    return Connection.SendPacket(static_cast<uint8>(EServerMessageType::MT_FunctionCall), PacketPayload.empty() ? nullptr : PacketPayload.data(), static_cast<uint32>(PacketPayload.size()));
}

/// @brief shared_ptr 重载;Connection 为空返回 false。
bool SendServerCallMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& PacketPayload) {
    return Connection ? SendServerCallMessage(*Connection, PacketPayload) : false;
}

/// @brief 通用连接版:先补 1 字节 MT_FunctionCall 再 Send。
bool SendServerCallMessage(INetConnection& Connection, const TByteArray& PacketPayload) {
    TByteArray Packet;
    Packet.reserve(1 + PacketPayload.size());
    Packet.push_back(static_cast<uint8>(EServerMessageType::MT_FunctionCall));
    Packet.insert(Packet.end(), PacketPayload.begin(), PacketPayload.end());
    return Connection.Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

/// @brief shared_ptr 重载;Connection 为空返回 false。
bool SendServerCallMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& PacketPayload) {
    return Connection ? SendServerCallMessage(*Connection, PacketPayload) : false;
}

/// @brief 走 MServerConnection::SendPacket(由它补 MT_FunctionResponse 类型字节)。
bool SendServerCallResponseMessage(MServerConnection& Connection, const TByteArray& PacketPayload) {
    return Connection.SendPacket(static_cast<uint8>(EServerMessageType::MT_FunctionResponse), PacketPayload.empty() ? nullptr : PacketPayload.data(), static_cast<uint32>(PacketPayload.size()));
}

/// @brief shared_ptr 重载;Connection 为空返回 false。
bool SendServerCallResponseMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& PacketPayload) {
    return Connection ? SendServerCallResponseMessage(*Connection, PacketPayload) : false;
}

/// @brief 通用连接版:先补 1 字节 MT_FunctionResponse 再 Send。
bool SendServerCallResponseMessage(INetConnection& Connection, const TByteArray& PacketPayload) {
    TByteArray Packet;
    Packet.reserve(1 + PacketPayload.size());
    Packet.push_back(static_cast<uint8>(EServerMessageType::MT_FunctionResponse));
    Packet.insert(Packet.end(), PacketPayload.begin(), PacketPayload.end());
    return Connection.Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

/// @brief shared_ptr 重载;Connection 为空返回 false。
bool SendServerCallResponseMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& PacketPayload) {
    return Connection ? SendServerCallResponseMessage(*Connection, PacketPayload) : false;
}
