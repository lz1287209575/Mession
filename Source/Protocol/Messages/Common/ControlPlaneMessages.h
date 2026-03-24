#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Net/ServerConnection.h"

MSTRUCT()
struct SEmptyServerMessage
{
};

MSTRUCT()
struct SNodeHandshakeMessage
{
    MPROPERTY()
    uint32 ServerId = 0;

    MPROPERTY()
    EServerType ServerType = EServerType::Unknown;

    MPROPERTY()
    MString ServerName;
};

MSTRUCT()
struct SNodeRegisterMessage
{
    MPROPERTY()
    uint32 ServerId = 0;

    MPROPERTY()
    EServerType ServerType = EServerType::Unknown;

    MPROPERTY()
    MString ServerName;

    MPROPERTY()
    MString Address;

    MPROPERTY()
    uint16 Port = 0;

    MPROPERTY()
    uint16 ZoneId = 0;
};

MSTRUCT()
struct SNodeRegisterAckMessage
{
    MPROPERTY()
    uint8 Result = 0;
};

MSTRUCT()
struct SNodeLoadReportMessage
{
    MPROPERTY()
    uint32 CurrentLoad = 0;

    MPROPERTY()
    uint32 Capacity = 0;
};

MSTRUCT()
struct SRouteQueryMessage
{
    MPROPERTY()
    uint64 RequestId = 0;

    MPROPERTY()
    EServerType RequestedType = EServerType::Unknown;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint16 ZoneId = 0;
};

MSTRUCT()
struct SRouteServerInfoMessage
{
    MPROPERTY()
    uint32 ServerId = 0;

    MPROPERTY()
    EServerType ServerType = EServerType::Unknown;

    MPROPERTY()
    MString ServerName;

    MPROPERTY()
    MString Address;

    MPROPERTY()
    uint16 Port = 0;

    MPROPERTY()
    uint16 ZoneId = 0;

    SRouteServerInfoMessage() = default;
    SRouteServerInfoMessage(const SServerInfo& InServerInfo)
        : ServerId(InServerInfo.ServerId)
        , ServerType(InServerInfo.ServerType)
        , ServerName(InServerInfo.ServerName)
        , Address(InServerInfo.Address)
        , Port(InServerInfo.Port)
        , ZoneId(InServerInfo.ZoneId)
    {
    }

    SRouteServerInfoMessage& operator=(const SServerInfo& InServerInfo)
    {
        ServerId = InServerInfo.ServerId;
        ServerType = InServerInfo.ServerType;
        ServerName = InServerInfo.ServerName;
        Address = InServerInfo.Address;
        Port = InServerInfo.Port;
        ZoneId = InServerInfo.ZoneId;
        return *this;
    }

    operator SServerInfo() const
    {
        return SServerInfo(ServerId, ServerType, ServerName, Address, Port, ZoneId);
    }
};

MSTRUCT()
struct SRouteResponseMessage
{
    MPROPERTY()
    uint64 RequestId = 0;

    MPROPERTY()
    EServerType RequestedType = EServerType::Unknown;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    bool bFound = false;

    MPROPERTY()
    SRouteServerInfoMessage ServerInfo;
};

MSTRUCT()
struct SHeartbeatMessage
{
    MPROPERTY()
    uint32 Sequence = 0;
};
