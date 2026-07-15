#include "Common/Net/ServiceDiscovery/Endpoint.h"

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
}

bool EncodeEndpoint(const FServiceEndpoint& Ep, TByteArray& Out)
{
    Out.clear();
    AppendFixedLE(Out, static_cast<uint32>(Ep.ServerType));
    AppendFixedLE(Out, Ep.ServerId);
    AppendString_LE(Out, Ep.Address);
    AppendFixedLE16(Out, Ep.Port);
    AppendFixedLE64(Out, Ep.LastHeartbeatMs);
    Out.push_back(Ep.bHealthy ? 1 : 0);
    AppendFixedLE16(Out, static_cast<uint16>(Ep.ActorIds.size()));
    for (uint64 ActorId : Ep.ActorIds)
    {
        AppendFixedLE64(Out, ActorId);
    }
    return true;
}

bool DecodeEndpoint(const TByteArray& In, size_t& Off, FServiceEndpoint& Ep)
{
    uint32 ServerTypeRaw = 0;
    if (!ReadFixedLE(In, Off, ServerTypeRaw)) return false;
    Ep.ServerType = static_cast<EServerType>(ServerTypeRaw);
    if (!ReadFixedLE(In, Off, Ep.ServerId)) return false;
    if (!ReadString_LE(In, Off, Ep.Address)) return false;
    if (!ReadFixedLE16(In, Off, Ep.Port)) return false;
    if (!ReadFixedLE64(In, Off, Ep.LastHeartbeatMs)) return false;
    if (Off + 1 > In.size()) return false;
    Ep.bHealthy = (In[Off++] != 0);
    uint16 NumActors = 0;
    if (!ReadFixedLE16(In, Off, NumActors)) return false;
    Ep.ActorIds.clear();
    Ep.ActorIds.reserve(NumActors);
    for (uint16 i = 0; i < NumActors; ++i)
    {
        uint64 A = 0;
        if (!ReadFixedLE64(In, Off, A)) return false;
        Ep.ActorIds.push_back(A);
    }
    return true;
}