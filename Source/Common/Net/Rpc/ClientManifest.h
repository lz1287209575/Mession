#pragma once

// ClientManifest — 客户端调用清单的薄包装层。
//
// MClientManifest 自身在 Source/Common/Net/Rpc/MClientManifest.generated.h 里
// 由 MHeaderTool 维护（静态声明 + definitions 由 .mgenerated.cpp 提供）。
//
// 当前 PoC 阶段仓库里没有任何 MFUNCTION(Client/ClientCall) 函数——
// GClientManifestEntries 是空数组；FindByFunctionId 会始终返回 nullptr。

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Net/Rpc/MClientManifest.generated.h"
#include "Common/Net/Rpc/RpcManifest.h"

#include <cstring>

inline const MClientManifest::SEntry* FindGlobalClientFunctionEntryById(uint16_t FunctionId)
{
    return MClientManifest::FindByFunctionId(FunctionId);
}

inline MClass* FindGlobalClientFunctionOwnerClassById(uint16_t FunctionId)
{
    const MClientManifest::SEntry* Entry = FindGlobalClientFunctionEntryById(FunctionId);
    if (!Entry || !Entry->OwnerType || Entry->OwnerType[0] == '\0')
    {
        return nullptr;
    }

    return MObject::FindClass(Entry->OwnerType);
}

inline const MFunction* FindGlobalClientFunctionById(uint16_t FunctionId)
{
    const MClientManifest::SEntry* Entry = FindGlobalClientFunctionEntryById(FunctionId);
    MClass* OwnerClass = FindGlobalClientFunctionOwnerClassById(FunctionId);
    if (!Entry || !OwnerClass || !Entry->FunctionName || Entry->FunctionName[0] == '\0')
    {
        return nullptr;
    }

    return OwnerClass->FindFunction(Entry->FunctionName);
}

inline MClass* FindGlobalClientResponseStructById(uint16_t FunctionId)
{
    const MClientManifest::SEntry* Entry = FindGlobalClientFunctionEntryById(FunctionId);
    if (!Entry || !Entry->ResponseTypeName || Entry->ResponseTypeName[0] == '\0')
    {
        return nullptr;
    }

    return MObject::FindStruct(Entry->ResponseTypeName);
}

inline EServerType GetGlobalClientFunctionTargetServerType(uint16_t FunctionId)
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
    if (std::strcmp(Entry->OwnerType, "MEchoService") == 0)
    {
        return EServerType::Echo;
    }

    return EServerType::Unknown;
}