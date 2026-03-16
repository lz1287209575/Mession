#pragma once

#include "NetDriver/Reflection.h"
#include "Common/ServerConnection.h"
#include "Core/Net/NetCore.h"

#include <cstddef>

bool BuildServerRpcPayload(uint16 FunctionId, const TArray& InPayload, TArray& OutData);

struct SRpcEndpointBinding
{
    EServerType ServerType = EServerType::Unknown;
    const char* ClassName = nullptr;
    const char* FunctionName = nullptr;
};

inline const SRpcEndpointBinding* FindRpcEndpointByServerType(
    const SRpcEndpointBinding* Bindings,
    size_t Count,
    EServerType ServerType)
{
    for (size_t Index = 0; Index < Count; ++Index)
    {
        if (Bindings[Index].ServerType == ServerType)
        {
            return &Bindings[Index];
        }
    }

    return nullptr;
}

bool BuildRpcPayloadForEndpoint(const SRpcEndpointBinding& Binding, const TArray& InPayload, TArray& OutData);
bool TryInvokeServerRpc(MReflectObject* ServiceInstance, const TArray& Data, ERpcType ExpectedType);
