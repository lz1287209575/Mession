#pragma once

#include "Common/Net/ServiceDiscovery/Endpoint.h"

namespace RegistryProtocol
{
// Each Build* function prepends a 1-byte PacketType (matching
// EServiceRegistryMessageType) so the resulting byte buffer is ready to be
// handed to MTcpConnection::Send (which then length-prefixes via
// MLengthPrefixedPacketCodec).
bool BuildRegistryRegisterPacket(const FServiceEndpoint& Endpoint, TByteArray& OutPacket);
bool BuildRegistryDeregisterPacket(uint32 ServerId, TByteArray& OutPacket);
bool BuildRegistryHeartbeatPacket(uint32 ServerId, uint64 TimestampMs, TByteArray& OutPacket);
bool BuildRegistryUpdateActorsPacket(uint32 ServerId, const TVector<uint64>& ActorIds, TByteArray& OutPacket);
bool BuildRegistryListEndpointsPacket(EServerType ServerType, TByteArray& OutPacket);
bool BuildRegistryEndpointChangePacket(EServerType ServerType, uint64 Seq, const TVector<FServiceEndpoint>& Endpoints, TByteArray& OutPacket);
bool BuildRegistryAckPacket(EServiceRegistryResult Status, const MString& Message, TByteArray& OutPacket);

// Parsers consume the payload AFTER the 1-byte PacketType has been stripped.
// The PacketType dispatch lives in the caller (ServiceRegistryServer / EndpointCache).
bool ParseRegistryRegisterPacket(const TByteArray& Payload, FServiceEndpoint& OutEndpoint);
bool ParseRegistryDeregisterPacket(const TByteArray& Payload, uint32& OutServerId);
bool ParseRegistryHeartbeatPacket(const TByteArray& Payload, uint32& OutServerId, uint64& OutTimestampMs);
bool ParseRegistryUpdateActorsPacket(const TByteArray& Payload, uint32& OutServerId, TVector<uint64>& OutActorIds);
bool ParseRegistryListEndpointsPacket(const TByteArray& Payload, EServerType& OutServerType);
bool ParseRegistryEndpointChangePacket(const TByteArray& Payload, EServerType& OutServerType, uint64& OutSeq, TVector<FServiceEndpoint>& OutEndpoints);
bool ParseRegistryAckPacket(const TByteArray& Payload, EServiceRegistryResult& OutStatus, MString& OutMessage);
}