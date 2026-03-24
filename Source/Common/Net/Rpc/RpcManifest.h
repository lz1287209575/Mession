#pragma once

#include "Common/Net/Rpc/RpcTransport.h"
#include "Common/Runtime/Reflect/Reflection.h"

#include <cstddef>
#include <utility>

struct SRpcEndpointBinding
{
    EServerType ServerType = EServerType::Unknown;
    const char* ClassName = nullptr;
    const char* FunctionName = nullptr;
};

struct SRpcUnsupportedStat
{
    EServerType ServerType = EServerType::Unknown;
    MString FunctionName;
    uint64 Count = 0;
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

EServerType ParseServerTargetType(const char* TargetName);
const char* GetServerTypeDisplayName(EServerType ServerType);
const char* GetServerEndpointClassName(EServerType ServerType);
bool BuildRpcPayloadForEndpoint(const SRpcEndpointBinding& Binding, const TByteArray& InPayload, TByteArray& OutData);
bool FindRpcEndpoint(EServerType ServerType, const char* FunctionName, SRpcEndpointBinding& OutBinding);
bool ServerSupportsRpc(EServerType ServerType, const char* FunctionName);
size_t GetRpcEntryCount();
TVector<MString> GetRpcFunctionNames(EServerType ServerType);
MString BuildRpcManifestJson(EServerType ServerType);
void ReportUnsupportedRpcEndpoint(EServerType ServerType, const char* FunctionName);
TVector<SRpcUnsupportedStat> GetRpcUnsupportedStats();
TVector<SRpcUnsupportedStat> GetRpcUnsupportedStats(EServerType ServerType);
MString BuildRpcUnsupportedStatsJson();
MString BuildRpcUnsupportedStatsJson(EServerType ServerType);

template<typename... TArgs>
inline bool BuildRpcPayloadForRemoteCall(const char* ClassName, const char* FunctionName, TByteArray& OutData, TArgs&&... Args)
{
    if (!ClassName || !FunctionName)
    {
        return false;
    }

    if (MClass* Class = MObject::FindClass(ClassName))
    {
        if (MFunction* Func = Class->FindFunction(FunctionName))
        {
            return BuildRpcPayloadForFunctionCall(Func, OutData, std::forward<TArgs>(Args)...);
        }
    }

    return BuildServerRpcPayload(
        MGET_STABLE_RPC_FUNCTION_ID(ClassName, FunctionName),
        BuildRpcArgsPayload(std::forward<TArgs>(Args)...),
        OutData);
}

template<typename... TArgs>
inline bool BuildRpcPayloadForEndpointCall(const SRpcEndpointBinding& Binding, TByteArray& OutData, TArgs&&... Args)
{
    return BuildRpcPayloadForRemoteCall(
        Binding.ClassName,
        Binding.FunctionName,
        OutData,
        std::forward<TArgs>(Args)...);
}

template<typename... TArgs>
inline bool BuildRpcPayloadForServer(EServerType ServerType, const char* FunctionName, TByteArray& OutData, TArgs&&... Args)
{
    SRpcEndpointBinding Binding;
    if (!FindRpcEndpoint(ServerType, FunctionName, Binding))
    {
        const char* ClassName = GetServerEndpointClassName(ServerType);
        if (!ClassName || !FunctionName)
        {
            ReportUnsupportedRpcEndpoint(ServerType, FunctionName);
            return false;
        }

        Binding.ServerType = ServerType;
        Binding.ClassName = ClassName;
        Binding.FunctionName = FunctionName;
    }

    return BuildRpcPayloadForEndpointCall(Binding, OutData, std::forward<TArgs>(Args)...);
}

namespace MRpc
{
template<typename... TArgs>
inline bool Call(MServerConnection& Connection, EServerType ServerType, const char* FunctionName, TArgs&&... Args)
{
    TByteArray RpcPayload;
    if (!BuildRpcPayloadForServer(ServerType, FunctionName, RpcPayload, std::forward<TArgs>(Args)...))
    {
        return false;
    }

    return SendServerRpcMessage(Connection, RpcPayload);
}

template<typename... TArgs>
inline bool Call(const TSharedPtr<MServerConnection>& Connection, EServerType ServerType, const char* FunctionName, TArgs&&... Args)
{
    if (!Connection)
    {
        return false;
    }

    return Call(*Connection, ServerType, FunctionName, std::forward<TArgs>(Args)...);
}

template<typename... TArgs>
inline bool Call(INetConnection& Connection, EServerType ServerType, const char* FunctionName, TArgs&&... Args)
{
    TByteArray RpcPayload;
    if (!BuildRpcPayloadForServer(ServerType, FunctionName, RpcPayload, std::forward<TArgs>(Args)...))
    {
        return false;
    }

    return SendServerRpcMessage(Connection, RpcPayload);
}

template<typename... TArgs>
inline bool Call(const TSharedPtr<INetConnection>& Connection, EServerType ServerType, const char* FunctionName, TArgs&&... Args)
{
    if (!Connection)
    {
        return false;
    }

    return Call(*Connection, ServerType, FunctionName, std::forward<TArgs>(Args)...);
}

template<typename... TArgs>
inline bool CallRemote(MServerConnection& Connection, const char* ClassName, const char* FunctionName, TArgs&&... Args)
{
    TByteArray RpcPayload;
    if (!BuildRpcPayloadForRemoteCall(ClassName, FunctionName, RpcPayload, std::forward<TArgs>(Args)...))
    {
        return false;
    }

    return SendServerRpcMessage(Connection, RpcPayload);
}

template<typename... TArgs>
inline bool CallRemote(const TSharedPtr<MServerConnection>& Connection, const char* ClassName, const char* FunctionName, TArgs&&... Args)
{
    if (!Connection)
    {
        return false;
    }

    return CallRemote(*Connection, ClassName, FunctionName, std::forward<TArgs>(Args)...);
}

template<typename... TArgs>
inline bool CallRemote(INetConnection& Connection, const char* ClassName, const char* FunctionName, TArgs&&... Args)
{
    TByteArray RpcPayload;
    if (!BuildRpcPayloadForRemoteCall(ClassName, FunctionName, RpcPayload, std::forward<TArgs>(Args)...))
    {
        return false;
    }

    return SendServerRpcMessage(Connection, RpcPayload);
}

template<typename... TArgs>
inline bool CallRemote(const TSharedPtr<INetConnection>& Connection, const char* ClassName, const char* FunctionName, TArgs&&... Args)
{
    if (!Connection)
    {
        return false;
    }

    return CallRemote(*Connection, ClassName, FunctionName, std::forward<TArgs>(Args)...);
}

template<typename... TArgs>
inline bool TryCall(MServerConnection& Connection, EServerType ServerType, const char* FunctionName, TArgs&&... Args)
{
    return Call(Connection, ServerType, FunctionName, std::forward<TArgs>(Args)...);
}

template<typename... TArgs>
inline bool TryCall(const TSharedPtr<MServerConnection>& Connection, EServerType ServerType, const char* FunctionName, TArgs&&... Args)
{
    return Call(Connection, ServerType, FunctionName, std::forward<TArgs>(Args)...);
}

template<typename... TArgs>
inline bool TryCall(INetConnection& Connection, EServerType ServerType, const char* FunctionName, TArgs&&... Args)
{
    return Call(Connection, ServerType, FunctionName, std::forward<TArgs>(Args)...);
}

template<typename... TArgs>
inline bool TryCall(const TSharedPtr<INetConnection>& Connection, EServerType ServerType, const char* FunctionName, TArgs&&... Args)
{
    return Call(Connection, ServerType, FunctionName, std::forward<TArgs>(Args)...);
}
} // namespace MRpc
