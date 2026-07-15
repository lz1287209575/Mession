#include "Common/Net/ServiceDiscovery/RegistryProtocol.h"

#include <cstring>

namespace
{
inline void AppendFixedLE(TByteArray& Out, uint32 V)
{
    const uint8* P = reinterpret_cast<const uint8*>(&V);
    Out.insert(Out.end(), P, P + sizeof(V));
}
inline void AppendFixedLE16(TByteArray& Out, uint16 V)
{
    const uint8* P = reinterpret_cast<const uint8*>(&V);
    Out.insert(Out.end(), P, P + sizeof(V));
}
inline void AppendFixedLE64(TByteArray& Out, uint64 V)
{
    const uint8* P = reinterpret_cast<const uint8*>(&V);
    Out.insert(Out.end(), P, P + sizeof(V));
}
inline bool ReadFixedLE(const TByteArray& In, size_t& Off, uint32& Out)
{
    if (Off + sizeof(uint32) > In.size()) return false;
    std::memcpy(&Out, In.data() + Off, sizeof(uint32));
    Off += sizeof(uint32);
    return true;
}
inline bool ReadFixedLE16(const TByteArray& In, size_t& Off, uint16& Out)
{
    if (Off + sizeof(uint16) > In.size()) return false;
    std::memcpy(&Out, In.data() + Off, sizeof(uint16));
    Off += sizeof(uint16);
    return true;
}
inline bool ReadFixedLE64(const TByteArray& In, size_t& Off, uint64& Out)
{
    if (Off + sizeof(uint64) > In.size()) return false;
    std::memcpy(&Out, In.data() + Off, sizeof(uint64));
    Off += sizeof(uint64);
    return true;
}
inline void AppendString_LE(TByteArray& Out, const MString& S)
{
    AppendFixedLE16(Out, static_cast<uint16>(S.size()));
    Out.insert(Out.end(), S.begin(), S.end());
}
inline bool ReadString_LE(const TByteArray& In, size_t& Off, MString& Out)
{
    uint16 Len = 0;
    if (!ReadFixedLE16(In, Off, Len)) return false;
    if (Off + Len > In.size()) return false;
    Out.assign(In.begin() + Off, In.begin() + Off + Len);
    Off += Len;
    return true;
}
inline void PrependType(TByteArray& Out, EServiceRegistryMessageType Type)
{
    Out.insert(Out.begin(), static_cast<uint8>(Type));
}
}

namespace RegistryProtocol
{
bool BuildRegistryRegisterPacket(const FServiceEndpoint& Endpoint, TByteArray& OutPacket)
{
    TByteArray Body;
    if (!EncodeEndpoint(Endpoint, Body)) return false;
    OutPacket = std::move(Body);
    PrependType(OutPacket, EServiceRegistryMessageType::Register);
    return true;
}

bool BuildRegistryDeregisterPacket(uint32 ServerId, TByteArray& OutPacket)
{
    OutPacket.clear();
    AppendFixedLE(OutPacket, ServerId);
    PrependType(OutPacket, EServiceRegistryMessageType::Deregister);
    return true;
}

bool BuildRegistryHeartbeatPacket(uint32 ServerId, uint64 TimestampMs, TByteArray& OutPacket)
{
    OutPacket.clear();
    AppendFixedLE(OutPacket, ServerId);
    AppendFixedLE64(OutPacket, TimestampMs);
    PrependType(OutPacket, EServiceRegistryMessageType::Heartbeat);
    return true;
}

bool BuildRegistryUpdateActorsPacket(uint32 ServerId, const TVector<uint64>& ActorIds, TByteArray& OutPacket)
{
    OutPacket.clear();
    AppendFixedLE(OutPacket, ServerId);
    AppendFixedLE16(OutPacket, static_cast<uint16>(ActorIds.size()));
    for (uint64 A : ActorIds) AppendFixedLE64(OutPacket, A);
    PrependType(OutPacket, EServiceRegistryMessageType::UpdateActors);
    return true;
}

bool BuildRegistryListEndpointsPacket(EServerType ServerType, TByteArray& OutPacket)
{
    OutPacket.clear();
    AppendFixedLE(OutPacket, static_cast<uint32>(ServerType));
    PrependType(OutPacket, EServiceRegistryMessageType::ListEndpoints);
    return true;
}

bool BuildRegistryEndpointChangePacket(EServerType ServerType, uint64 Seq, const TVector<FServiceEndpoint>& Endpoints, TByteArray& OutPacket)
{
    OutPacket.clear();
    AppendFixedLE(OutPacket, static_cast<uint32>(ServerType));
    AppendFixedLE64(OutPacket, Seq);
    AppendFixedLE16(OutPacket, static_cast<uint16>(Endpoints.size()));
    for (const FServiceEndpoint& Ep : Endpoints)
    {
        TByteArray Encoded;
        if (!EncodeEndpoint(Ep, Encoded)) return false;
        AppendFixedLE16(OutPacket, static_cast<uint16>(Encoded.size()));
        OutPacket.insert(OutPacket.end(), Encoded.begin(), Encoded.end());
    }
    PrependType(OutPacket, EServiceRegistryMessageType::EndpointChange);
    return true;
}

bool BuildRegistryAckPacket(EServiceRegistryResult Status, const MString& Message, TByteArray& OutPacket)
{
    OutPacket.clear();
    OutPacket.push_back(static_cast<uint8>(Status));
    AppendString_LE(OutPacket, Message);
    PrependType(OutPacket, EServiceRegistryMessageType::Ack);
    return true;
}

bool ParseRegistryRegisterPacket(const TByteArray& Payload, FServiceEndpoint& OutEndpoint)
{
    size_t Off = 0;
    return DecodeEndpoint(Payload, Off, OutEndpoint) && Off == Payload.size();
}

bool ParseRegistryDeregisterPacket(const TByteArray& Payload, uint32& OutServerId)
{
    size_t Off = 0;
    return ReadFixedLE(Payload, Off, OutServerId) && Off == Payload.size();
}

bool ParseRegistryHeartbeatPacket(const TByteArray& Payload, uint32& OutServerId, uint64& OutTimestampMs)
{
    size_t Off = 0;
    return ReadFixedLE(Payload, Off, OutServerId) && ReadFixedLE64(Payload, Off, OutTimestampMs) && Off == Payload.size();
}

bool ParseRegistryUpdateActorsPacket(const TByteArray& Payload, uint32& OutServerId, TVector<uint64>& OutActorIds)
{
    size_t Off = 0;
    if (!ReadFixedLE(Payload, Off, OutServerId)) return false;
    uint16 Count = 0;
    if (!ReadFixedLE16(Payload, Off, Count)) return false;
    OutActorIds.clear();
    OutActorIds.reserve(Count);
    for (uint16 i = 0; i < Count; ++i)
    {
        uint64 A = 0;
        if (!ReadFixedLE64(Payload, Off, A)) return false;
        OutActorIds.push_back(A);
    }
    return Off == Payload.size();
}

bool ParseRegistryListEndpointsPacket(const TByteArray& Payload, EServerType& OutServerType)
{
    size_t Off = 0;
    uint32 Raw = 0;
    if (!ReadFixedLE(Payload, Off, Raw)) return false;
    OutServerType = static_cast<EServerType>(Raw);
    return Off == Payload.size();
}

bool ParseRegistryEndpointChangePacket(const TByteArray& Payload, EServerType& OutServerType, uint64& OutSeq, TVector<FServiceEndpoint>& OutEndpoints)
{
    size_t Off = 0;
    uint32 ServerTypeRaw = 0;
    if (!ReadFixedLE(Payload, Off, ServerTypeRaw)) return false;
    OutServerType = static_cast<EServerType>(ServerTypeRaw);
    if (!ReadFixedLE64(Payload, Off, OutSeq)) return false;
    uint16 Count = 0;
    if (!ReadFixedLE16(Payload, Off, Count)) return false;
    OutEndpoints.clear();
    OutEndpoints.reserve(Count);
    for (uint16 i = 0; i < Count; ++i)
    {
        uint16 EncodedSize = 0;
        if (!ReadFixedLE16(Payload, Off, EncodedSize)) return false;
        if (Off + EncodedSize > Payload.size()) return false;
        TByteArray Encoded(Payload.begin() + Off, Payload.begin() + Off + EncodedSize);
        Off += EncodedSize;
        size_t InnerOff = 0;
        FServiceEndpoint Ep;
        if (!DecodeEndpoint(Encoded, InnerOff, Ep)) return false;
        OutEndpoints.push_back(std::move(Ep));
    }
    return Off == Payload.size();
}

bool ParseRegistryAckPacket(const TByteArray& Payload, EServiceRegistryResult& OutStatus, MString& OutMessage)
{
    if (Payload.empty()) return false;
    size_t Off = 0;
    OutStatus = static_cast<EServiceRegistryResult>(Payload[Off++]);
    return ReadString_LE(Payload, Off, OutMessage) && Off == Payload.size();
}
}