#pragma once

#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Reflect/Reflection.h"

enum class EObjectCallRootType : uint8
{
    Unknown = 0,
    Player = 1,
};

MSTRUCT()
struct FObjectCallTarget
{
    MPROPERTY()
    EObjectCallRootType RootType = EObjectCallRootType::Unknown;

    MPROPERTY()
    uint64 RootId = 0;

    MPROPERTY()
    MString ObjectPath;

    MPROPERTY()
    EServerType TargetServerType = EServerType::Unknown;
};

MSTRUCT()
struct FObjectCallRequest
{
    MPROPERTY()
    FObjectCallTarget Target;

    MPROPERTY()
    MString FunctionName;

    MPROPERTY()
    TByteArray Payload;
};

MSTRUCT()
struct FObjectCallResponse
{
    MPROPERTY()
    TByteArray Payload;
};
