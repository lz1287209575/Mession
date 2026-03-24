#include "Common/Net/Rpc/RpcTransport.h"

#include <cstring>

bool BuildServerRpcPayload(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutData)
{
    const uint32 PayloadSize = static_cast<uint32>(InPayload.size());

    OutData.clear();
    OutData.reserve(sizeof(FunctionId) + sizeof(PayloadSize) + PayloadSize);

    const uint8* FuncPtr = reinterpret_cast<const uint8*>(&FunctionId);
    OutData.insert(OutData.end(), FuncPtr, FuncPtr + sizeof(FunctionId));

    const uint8* SizePtr = reinterpret_cast<const uint8*>(&PayloadSize);
    OutData.insert(OutData.end(), SizePtr, SizePtr + sizeof(PayloadSize));

    if (PayloadSize > 0)
    {
        OutData.insert(OutData.end(), InPayload.begin(), InPayload.end());
    }

    return true;
}

bool BuildServerRpcMessage(const TByteArray& RpcPayload, TByteArray& OutPacket)
{
    OutPacket.clear();
    OutPacket.reserve(1 + RpcPayload.size());
    OutPacket.push_back(static_cast<uint8>(EServerMessageType::MT_RPC));
    OutPacket.insert(OutPacket.end(), RpcPayload.begin(), RpcPayload.end());
    return true;
}

bool SendServerRpcMessage(MServerConnection& Connection, const TByteArray& RpcPayload)
{
    return Connection.SendPacket(
        static_cast<uint8>(EServerMessageType::MT_RPC),
        RpcPayload.empty() ? nullptr : RpcPayload.data(),
        static_cast<uint32>(RpcPayload.size()));
}

bool SendServerRpcMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& RpcPayload)
{
    return Connection ? SendServerRpcMessage(*Connection, RpcPayload) : false;
}

bool SendServerRpcMessage(INetConnection& Connection, const TByteArray& RpcPayload)
{
    TByteArray Packet;
    BuildServerRpcMessage(RpcPayload, Packet);
    return Connection.Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

bool SendServerRpcMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& RpcPayload)
{
    return Connection ? SendServerRpcMessage(*Connection, RpcPayload) : false;
}

bool BuildClientFunctionPacket(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutPacket)
{
    if (FunctionId == 0)
    {
        return false;
    }

    OutPacket.clear();
    OutPacket.reserve(1 + sizeof(FunctionId) + sizeof(uint32) + InPayload.size());
    OutPacket.push_back(static_cast<uint8>(EClientMessageType::MT_FunctionCall));
    AppendValue(OutPacket, FunctionId);
    AppendValue(OutPacket, static_cast<uint32>(InPayload.size()));
    OutPacket.insert(OutPacket.end(), InPayload.begin(), InPayload.end());
    return true;
}

bool BuildClientCallPacket(uint16 FunctionId, uint64 CallId, const TByteArray& InPayload, TByteArray& OutPacket)
{
    if (FunctionId == 0)
    {
        return false;
    }

    OutPacket.clear();
    OutPacket.reserve(1 + sizeof(FunctionId) + sizeof(CallId) + sizeof(uint32) + InPayload.size());
    OutPacket.push_back(static_cast<uint8>(EClientMessageType::MT_FunctionCall));
    AppendValue(OutPacket, FunctionId);
    AppendValue(OutPacket, CallId);
    AppendValue(OutPacket, static_cast<uint32>(InPayload.size()));
    OutPacket.insert(OutPacket.end(), InPayload.begin(), InPayload.end());
    return true;
}

bool ParseClientFunctionPacket(const TByteArray& Data, uint16& OutFunctionId, uint32& OutPayloadSize, size_t& OutPayloadOffset)
{
    const size_t HeaderSize = 1 + sizeof(uint16) + sizeof(uint32);
    if (Data.size() < HeaderSize)
    {
        return false;
    }

    std::memcpy(&OutFunctionId, Data.data() + 1, sizeof(OutFunctionId));
    std::memcpy(&OutPayloadSize, Data.data() + 1 + sizeof(uint16), sizeof(OutPayloadSize));
    OutPayloadOffset = HeaderSize;
    return Data.size() >= OutPayloadOffset + static_cast<size_t>(OutPayloadSize);
}

bool ParseClientCallPacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, uint32& OutPayloadSize, size_t& OutPayloadOffset)
{
    const size_t HeaderSize = 1 + sizeof(uint16) + sizeof(uint64) + sizeof(uint32);
    if (Data.size() < HeaderSize)
    {
        return false;
    }

    std::memcpy(&OutFunctionId, Data.data() + 1, sizeof(OutFunctionId));
    std::memcpy(&OutCallId, Data.data() + 1 + sizeof(uint16), sizeof(OutCallId));
    std::memcpy(&OutPayloadSize, Data.data() + 1 + sizeof(uint16) + sizeof(uint64), sizeof(OutPayloadSize));
    OutPayloadOffset = HeaderSize;
    return Data.size() >= OutPayloadOffset + static_cast<size_t>(OutPayloadSize);
}

bool BuildServerCallPacket(uint16 FunctionId, uint64 CallId, const TByteArray& InPayload, TByteArray& OutPayload)
{
    if (FunctionId == 0 || CallId == 0)
    {
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

bool BuildServerCallResponsePacket(uint16 FunctionId, uint64 CallId, bool bSuccess, const TByteArray& InPayload, TByteArray& OutPayload)
{
    if (FunctionId == 0 || CallId == 0)
    {
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

bool ParseServerCallPacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, uint32& OutPayloadSize, size_t& OutPayloadOffset)
{
    const size_t HeaderSize = sizeof(uint16) + sizeof(uint64) + sizeof(uint32);
    if (Data.size() < HeaderSize)
    {
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

bool ParseServerCallResponsePacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, bool& OutSuccess, uint32& OutPayloadSize, size_t& OutPayloadOffset)
{
    const size_t HeaderSize = sizeof(uint16) + sizeof(uint64) + sizeof(uint8) + sizeof(uint32);
    if (Data.size() < HeaderSize)
    {
        return false;
    }

    size_t Offset = 0;
    uint8 SuccessByte = 0;
    std::memcpy(&OutFunctionId, Data.data() + Offset, sizeof(OutFunctionId));
    Offset += sizeof(OutFunctionId);
    std::memcpy(&OutCallId, Data.data() + Offset, sizeof(OutCallId));
    Offset += sizeof(OutCallId);
    std::memcpy(&SuccessByte, Data.data() + Offset, sizeof(SuccessByte));
    Offset += sizeof(SuccessByte);
    std::memcpy(&OutPayloadSize, Data.data() + Offset, sizeof(OutPayloadSize));
    Offset += sizeof(OutPayloadSize);
    OutSuccess = SuccessByte != 0;
    OutPayloadOffset = Offset;
    return Data.size() >= OutPayloadOffset + static_cast<size_t>(OutPayloadSize);
}

bool SendServerCallMessage(MServerConnection& Connection, const TByteArray& PacketPayload)
{
    return Connection.SendPacket(
        static_cast<uint8>(EServerMessageType::MT_FunctionCall),
        PacketPayload.empty() ? nullptr : PacketPayload.data(),
        static_cast<uint32>(PacketPayload.size()));
}

bool SendServerCallMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& PacketPayload)
{
    return Connection ? SendServerCallMessage(*Connection, PacketPayload) : false;
}

bool SendServerCallMessage(INetConnection& Connection, const TByteArray& PacketPayload)
{
    TByteArray Packet;
    Packet.reserve(1 + PacketPayload.size());
    Packet.push_back(static_cast<uint8>(EServerMessageType::MT_FunctionCall));
    Packet.insert(Packet.end(), PacketPayload.begin(), PacketPayload.end());
    return Connection.Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

bool SendServerCallMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& PacketPayload)
{
    return Connection ? SendServerCallMessage(*Connection, PacketPayload) : false;
}

bool SendServerCallResponseMessage(MServerConnection& Connection, const TByteArray& PacketPayload)
{
    return Connection.SendPacket(
        static_cast<uint8>(EServerMessageType::MT_FunctionResponse),
        PacketPayload.empty() ? nullptr : PacketPayload.data(),
        static_cast<uint32>(PacketPayload.size()));
}

bool SendServerCallResponseMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& PacketPayload)
{
    return Connection ? SendServerCallResponseMessage(*Connection, PacketPayload) : false;
}

bool SendServerCallResponseMessage(INetConnection& Connection, const TByteArray& PacketPayload)
{
    TByteArray Packet;
    Packet.reserve(1 + PacketPayload.size());
    Packet.push_back(static_cast<uint8>(EServerMessageType::MT_FunctionResponse));
    Packet.insert(Packet.end(), PacketPayload.begin(), PacketPayload.end());
    return Connection.Send(Packet.data(), static_cast<uint32>(Packet.size()));
}

bool SendServerCallResponseMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& PacketPayload)
{
    return Connection ? SendServerCallResponseMessage(*Connection, PacketPayload) : false;
}
