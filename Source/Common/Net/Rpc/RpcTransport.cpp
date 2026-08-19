#include "Common/Net/Rpc/RpcTransport.h"

#include <cstring>

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

bool BuildServerRpcMessage(const TByteArray& RpcPayload, TByteArray& OutPacket) {
    OutPacket.clear();
    OutPacket.reserve(1 + RpcPayload.size());
    OutPacket.push_back(static_cast<uint8>(EServerMessageType::MT_RPC));
    OutPacket.insert(OutPacket.end(), RpcPayload.begin(), RpcPayload.end());
    return true;
}

bool SendServerRpcMessage(MServerConnection& Connection, const TByteArray& RpcPayload) {
    return Connection.SendPacket(static_cast<uint8>(EServerMessageType::MT_RPC), RpcPayload.empty() ? nullptr : RpcPayload.data(), static_cast<uint32>(RpcPayload.size()));
}

bool SendServerRpcMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& RpcPayload) {
    return Connection ? SendServerRpcMessage(*Connection, RpcPayload) : false;
}

bool SendServerRpcMessage(INetConnection& Connection, const TByteArray& RpcPayload) {
    TByteArray Packet;
    BuildServerRpcMessage(RpcPayload, Packet);
    return Connection.Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

bool SendServerRpcMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& RpcPayload) {
    return Connection ? SendServerRpcMessage(*Connection, RpcPayload) : false;
}

// Client↔Gateway envelope:无 MessageType 字节,纯反射。
//
// wire 形态(全长 length-prefix 由 MLengthPrefixedPacketCodec 处理):
//   [RequestId: 8B][FunctionId: 2B][PayloadSize: 4B][Payload: N]
//
// RequestId 由 UE 客户端唯一分配,Gateway 透传不解释:
//   - 普通上行请求 → UE 给一个 id,Gateway 拿到后塞同一个 id 回包
//   - 服务器主动 push（下行）→ RequestId = 0 表示 push（见
//     GatewayServer::PushClientDownlink 与 RpcClientCall::SendClientDownlink，
//     与 RequestId!=0 的请求/响应区分）
// UE 协议族的"Request/Response/Push"区分完全靠 RequestId + 自身 manifest,
// Gateway 不做 enum 分发(架构决定,见 TODO/architecture refactor)。

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

// Legacy wrappers — 后续步骤 2 会彻底删,这里先保留以保证
// 当前 GatewayServer.cpp 的 HandleClientPacket 不破编译。
// 旧 BuildClientFunctionPacket / BuildClientCallPacket / ParseClientCallPacket
// / ParseClientFunctionPacket 等价于此版本的 envelope,但附了原 1 字节
// MessageType 头(MT_FunctionCall=13)。Gateway 那一侧还没切,先用旧
// 形式糊住。

bool BuildClientFunctionPacket(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutPacket) {
    // step-2: 旧 BuildClientFunctionPacket(1-byte MessageType=MT_FunctionCall=13)
    // 整体删除;统一用 BuildClientEnvelopePacket。这里保留空函数体是为了兼容
    // 第三方 stub / 老 validate.py 子模块的引用,如有再彻底移除。
    (void)FunctionId;
    (void)InPayload;
    OutPacket.clear();
    return false;
}

bool BuildClientCallPacket(uint16 FunctionId, uint64 CallId, const TByteArray& InPayload, TByteArray& OutPacket) {
    // step-2: 旧 BuildClientCallPacket 整体删除。
    (void)FunctionId;
    (void)CallId;
    (void)InPayload;
    OutPacket.clear();
    return false;
}

bool ParseClientFunctionPacket(const TByteArray& Data, uint16& OutFunctionId, uint32& OutPayloadSize, size_t& OutPayloadOffset) {
    // step-2: 旧 ParseClientFunctionPacket 整体删除。Gateway 已切到
    // ParseClientEnvelopePacket。下面这几行保留兼容签名。
    (void)Data;
    (void)OutFunctionId;
    (void)OutPayloadSize;
    (void)OutPayloadOffset;
    return false;
}

bool ParseClientCallPacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, uint32& OutPayloadSize, size_t& OutPayloadOffset) {
    // step-2: 旧 ParseClientCallPacket 整体删除。
    (void)Data;
    (void)OutFunctionId;
    (void)OutCallId;
    (void)OutPayloadSize;
    (void)OutPayloadOffset;
    return false;
}

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

bool SendServerCallMessage(MServerConnection& Connection, const TByteArray& PacketPayload) {
    return Connection.SendPacket(static_cast<uint8>(EServerMessageType::MT_FunctionCall), PacketPayload.empty() ? nullptr : PacketPayload.data(), static_cast<uint32>(PacketPayload.size()));
}

bool SendServerCallMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& PacketPayload) {
    return Connection ? SendServerCallMessage(*Connection, PacketPayload) : false;
}

bool SendServerCallMessage(INetConnection& Connection, const TByteArray& PacketPayload) {
    TByteArray Packet;
    Packet.reserve(1 + PacketPayload.size());
    Packet.push_back(static_cast<uint8>(EServerMessageType::MT_FunctionCall));
    Packet.insert(Packet.end(), PacketPayload.begin(), PacketPayload.end());
    return Connection.Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

bool SendServerCallMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& PacketPayload) {
    return Connection ? SendServerCallMessage(*Connection, PacketPayload) : false;
}

bool SendServerCallResponseMessage(MServerConnection& Connection, const TByteArray& PacketPayload) {
    return Connection.SendPacket(static_cast<uint8>(EServerMessageType::MT_FunctionResponse), PacketPayload.empty() ? nullptr : PacketPayload.data(), static_cast<uint32>(PacketPayload.size()));
}

bool SendServerCallResponseMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& PacketPayload) {
    return Connection ? SendServerCallResponseMessage(*Connection, PacketPayload) : false;
}

bool SendServerCallResponseMessage(INetConnection& Connection, const TByteArray& PacketPayload) {
    TByteArray Packet;
    Packet.reserve(1 + PacketPayload.size());
    Packet.push_back(static_cast<uint8>(EServerMessageType::MT_FunctionResponse));
    Packet.insert(Packet.end(), PacketPayload.begin(), PacketPayload.end());
    return Connection.Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

bool SendServerCallResponseMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& PacketPayload) {
    return Connection ? SendServerCallResponseMessage(*Connection, PacketPayload) : false;
}
