#pragma once

// ClientManifest — 客户端调用清单的薄包装层。
//
// MClientManifest 自身在 Source/Common/Net/Rpc/MClientManifest.generated.h 里
// 由 MHeaderTool 维护（静态声明 + definitions 由 .mgenerated.cpp 提供）。
//
// 条目（GClientManifestEntries）由 MHeaderTool 生成器产出：当前 PoC 阶段
// 业务侧没有 MFUNCTION(Client/ClientCall) 声明，生成器补收集"客户端可经
// Gateway 调用的 ServerCall"（如 MEchoService.Echo / EchoAwait），使 Gateway
// 按 FunctionId 路由到后端服务（OwnerType + FunctionName）。FindByFunctionId
// 命中返回对应条目，未命中返回 nullptr（Gateway 记 manifest miss 并丢弃）。
// 后续若引入 MFUNCTION(Client/ClientCall)，条目将由这些声明直接 emit。

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