#pragma once

#include "Common/Net/Rpc/RpcPayload.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Serialization/MessageUtils.h"

#include <cstddef>

bool BuildServerRpcPayload(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutData);
bool BuildServerRpcMessage(const TByteArray& RpcPayload, TByteArray& OutPacket);
bool SendServerRpcMessage(MServerConnection& Connection, const TByteArray& RpcPayload);
bool SendServerRpcMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& RpcPayload);
bool SendServerRpcMessage(INetConnection& Connection, const TByteArray& RpcPayload);
bool SendServerRpcMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& RpcPayload);

bool BuildClientFunctionPacket(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutPacket);
bool BuildClientCallPacket(uint16 FunctionId, uint64 CallId, const TByteArray& InPayload, TByteArray& OutPacket);
bool ParseClientFunctionPacket(const TByteArray& Data, uint16& OutFunctionId, uint32& OutPayloadSize, size_t& OutPayloadOffset);
bool ParseClientCallPacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, uint32& OutPayloadSize, size_t& OutPayloadOffset);

// New step-1 envelope:无 MessageType 字节,纯反射 envelope。
// wire 形态:[RequestId:8B][FunctionId:2B][PayloadSize:4B][Payload:N]
// 最终取代上述 BuildClient*Packet / ParseClient*Packet。
bool BuildClientEnvelopePacket(uint16 FunctionId, uint64 RequestId, const TByteArray& InPayload, TByteArray& OutPacket);
bool ParseClientEnvelopePacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutRequestId, uint32& OutPayloadSize, size_t& OutPayloadOffset);

bool BuildServerCallPacket(uint16 FunctionId, uint64 CallId, const TByteArray& InPayload, TByteArray& OutPayload);
bool BuildServerCallResponsePacket(uint16 FunctionId, uint64 CallId, bool bSuccess, const TByteArray& InPayload, TByteArray& OutPayload);
bool ParseServerCallPacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, uint32& OutPayloadSize, size_t& OutPayloadOffset);
bool ParseServerCallResponsePacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, bool& OutSuccess, uint32& OutPayloadSize, size_t& OutPayloadOffset);
bool SendServerCallMessage(MServerConnection& Connection, const TByteArray& PacketPayload);
bool SendServerCallMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& PacketPayload);
bool SendServerCallMessage(INetConnection& Connection, const TByteArray& PacketPayload);
bool SendServerCallMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& PacketPayload);
bool SendServerCallResponseMessage(MServerConnection& Connection, const TByteArray& PacketPayload);
bool SendServerCallResponseMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& PacketPayload);
bool SendServerCallResponseMessage(INetConnection& Connection, const TByteArray& PacketPayload);
bool SendServerCallResponseMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& PacketPayload);
