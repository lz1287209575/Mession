#pragma once

#include "Common/Net/Rpc/RpcManifest.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "MClientManifest.generated.h"

inline const MClientManifest::SEntry* FindGlobalClientFunctionEntryById(uint16 FunctionId)
{
    return MClientManifest::FindByFunctionId(FunctionId);
}

inline MClass* FindGlobalClientFunctionOwnerClassById(uint16 FunctionId)
{
    const MClientManifest::SEntry* Entry = FindGlobalClientFunctionEntryById(FunctionId);
    if (!Entry || !Entry->OwnerType || Entry->OwnerType[0] == '\0')
    {
        return nullptr;
    }

    return MObject::FindClass(Entry->OwnerType);
}

inline const MFunction* FindGlobalClientFunctionById(uint16 FunctionId)
{
    const MClientManifest::SEntry* Entry = FindGlobalClientFunctionEntryById(FunctionId);
    MClass* OwnerClass = FindGlobalClientFunctionOwnerClassById(FunctionId);
    if (!Entry || !OwnerClass || !Entry->FunctionName || Entry->FunctionName[0] == '\0')
    {
        return nullptr;
    }

    return OwnerClass->FindFunction(Entry->FunctionName);
}

inline MClass* FindGlobalClientResponseStructById(uint16 FunctionId)
{
    const MClientManifest::SEntry* Entry = FindGlobalClientFunctionEntryById(FunctionId);
    if (!Entry || !Entry->ResponseTypeName || Entry->ResponseTypeName[0] == '\0')
    {
        return nullptr;
    }

    return MObject::FindStruct(Entry->ResponseTypeName);
}

inline EServerType GetGlobalClientFunctionTargetServerType(uint16 FunctionId)
{
    const MClientManifest::SEntry* Entry = FindGlobalClientFunctionEntryById(FunctionId);
    if (!Entry)
    {
        return EServerType::Unknown;
    }

    if (Entry->TargetName && Entry->TargetName[0] != '\0')
    {
        const EServerType TargetServerType = ParseServerTargetType(Entry->TargetName);
        if (TargetServerType != EServerType::Unknown)
        {
            return TargetServerType;
        }
    }

    if (!Entry->OwnerType || Entry->OwnerType[0] == '\0')
    {
        return EServerType::Unknown;
    }

    if (std::strcmp(Entry->OwnerType, "MGatewayServer") == 0)
    {
        return EServerType::Gateway;
    }
    if (std::strcmp(Entry->OwnerType, "MLoginServer") == 0)
    {
        return EServerType::Login;
    }
    if (std::strcmp(Entry->OwnerType, "MWorldServer") == 0)
    {
        return EServerType::World;
    }
    if (std::strcmp(Entry->OwnerType, "MSceneServer") == 0)
    {
        return EServerType::Scene;
    }
    if (std::strcmp(Entry->OwnerType, "MRouterServer") == 0)
    {
        return EServerType::Router;
    }
    if (std::strcmp(Entry->OwnerType, "MMgoServer") == 0)
    {
        return EServerType::Mgo;
    }

    return EServerType::Unknown;
}
