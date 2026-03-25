#pragma once

#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Reflect/Reflection.h"

enum class EObjectProxyRootType : uint8
{
    Unknown = 0,
    Player = 1,
};

MSTRUCT()
struct FObjectProxyTarget
{
    MPROPERTY()
    EObjectProxyRootType RootType = EObjectProxyRootType::Unknown;

    MPROPERTY()
    uint64 RootId = 0;

    MPROPERTY()
    MString ObjectPath;

    MPROPERTY()
    EServerType TargetServerType = EServerType::Unknown;
};

MSTRUCT()
struct FObjectProxyInvokeRequest
{
    MPROPERTY()
    FObjectProxyTarget Target;

    MPROPERTY()
    MString FunctionName;

    MPROPERTY()
    TByteArray Payload;
};

MSTRUCT()
struct FObjectProxyInvokeResponse
{
    MPROPERTY()
    TByteArray Payload;
};
