#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Net/ServerConnection.h"  // EServerType
#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FServiceEndpoint
{
    MPROPERTY() EServerType ServerType = EServerType::Unknown;
    MPROPERTY() uint32 ServerId = 0;
    MPROPERTY() MString Address = "0.0.0.0";
    MPROPERTY() uint16 Port = 0;
    MPROPERTY() uint64 LastHeartbeatMs = 0;
    MPROPERTY() bool bHealthy = true;
    MPROPERTY() TVector<uint64> ActorIds;
};

// Wire-level enum for the Registry protocol. Lives outside EServerMessageType /
// EClientMessageType so we can hand MServerConnection the raw uint8 PacketType
// without colliding with MT_RPC=27 / MT_FunctionCall=28 / MT_FunctionResponse=29
// (see Common/Net/ServerConnection.h:26-31). Base 200 is well clear of those.
enum class EServiceRegistryMessageType : uint8_t
{
    Register       = 200,
    Deregister     = 201,
    Heartbeat      = 202,
    UpdateActors   = 203,
    ListEndpoints  = 204,
    EndpointChange = 205,
    Ack            = 206,
};

enum class EServiceRegistryResult : uint8_t
{
    Ok             = 0,
    AlreadyExists  = 1,
    NotFound       = 2,
    InvalidPayload = 3,
    StaleSeq       = 4,
};

bool EncodeEndpoint(const FServiceEndpoint& Ep, TByteArray& Out);
bool DecodeEndpoint(const TByteArray& In, size_t& Off, FServiceEndpoint& Ep);