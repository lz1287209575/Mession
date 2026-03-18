#pragma once

#include "Common/ServerMessages.h"
#include "NetDriver/Reflection.h"
#include "Common/ServerConnection.h"
#include "Core/Net/NetCore.h"
#include "Messages/NetMessages.h"

#include <cstddef>
#include <utility>

bool BuildServerRpcPayload(uint16 FunctionId, const TArray& InPayload, TArray& OutData);
bool BuildServerRpcMessage(const TArray& RpcPayload, TArray& OutPacket);
bool SendServerRpcMessage(MServerConnection& Connection, const TArray& RpcPayload);
bool SendServerRpcMessage(const TSharedPtr<MServerConnection>& Connection, const TArray& RpcPayload);
bool SendServerRpcMessage(INetConnection& Connection, const TArray& RpcPayload);
bool SendServerRpcMessage(const TSharedPtr<INetConnection>& Connection, const TArray& RpcPayload);

struct SRpcEndpointBinding
{
    EServerType ServerType = EServerType::Unknown;
    const char* ClassName = nullptr;
    const char* FunctionName = nullptr;
};

struct SGeneratedRpcUnsupportedStat
{
    EServerType ServerType = EServerType::Unknown;
    FString FunctionName;
    uint64 Count = 0;
};

struct SGeneratedClientRouteRequest
{
    enum class ERouteKind : uint8
    {
        None = 0,
        Login = 1,
        World = 2,
        RouterResolved = 3,
    };

    uint64 ConnectionId = 0;
    EClientMessageType MessageType = EClientMessageType::MT_Error;
    const char* FunctionName = nullptr;
    ERouteKind RouteKind = ERouteKind::None;
    const char* RouteName = nullptr;
    EServerType TargetServerType = EServerType::Unknown;
    const char* TargetName = nullptr;
    const char* AuthMode = nullptr;
    const char* WrapMode = nullptr;
    const TArray* Payload = nullptr;
};

namespace MClientDownlink
{
inline constexpr const char* ScopeName = "MClientDownlink";
inline constexpr const char* OnLoginResponse = "Client_OnLoginResponse";
inline constexpr const char* OnActorCreate = "Client_OnActorCreate";
inline constexpr const char* OnActorUpdate = "Client_OnActorUpdate";
inline constexpr const char* OnActorDestroy = "Client_OnActorDestroy";
}

enum class EGeneratedClientDispatchResult : uint8
{
    NotFound = 0,
    Routed = 1,
    Handled = 2,
    RouteTargetUnsupported = 3,
    MissingFunction = 4,
    MissingBinder = 5,
    ParamBindingFailed = 6,
    InvokeFailed = 7,
    AuthRequired = 8,
    RoutePending = 9,
    BackendUnavailable = 10,
};

struct SGeneratedClientDispatchOutcome
{
    EGeneratedClientDispatchResult Result = EGeneratedClientDispatchResult::NotFound;
    const char* OwnerType = nullptr;
    const char* FunctionName = nullptr;
};

class IGeneratedClientRouteTarget
{
public:
    virtual ~IGeneratedClientRouteTarget() = default;
    virtual EGeneratedClientDispatchResult HandleGeneratedClientRoute(const SGeneratedClientRouteRequest& Request) = 0;
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
bool TryDispatchGeneratedClientMessage(
    MReflectObject* TargetInstance,
    uint64 ConnectionId,
    EClientMessageType MessageType,
    const TArray& Payload);
SGeneratedClientDispatchOutcome DispatchGeneratedClientMessage(
    MReflectObject* TargetInstance,
    uint64 ConnectionId,
    EClientMessageType MessageType,
    const TArray& Payload);
SGeneratedClientDispatchOutcome DispatchGeneratedClientFunction(
    MReflectObject* TargetInstance,
    uint64 ConnectionId,
    uint16 FunctionId,
    const TArray& Payload);
const char* GetServerTypeDisplayName(EServerType ServerType);
bool FindGeneratedRpcEndpoint(EServerType ServerType, const char* FunctionName, SRpcEndpointBinding& OutBinding);
bool ServerSupportsGeneratedRpc(EServerType ServerType, const char* FunctionName);
TVector<FString> GetGeneratedRpcFunctionNames(EServerType ServerType);
FString BuildGeneratedRpcManifestJson(EServerType ServerType);
void ReportUnsupportedGeneratedRpcEndpoint(EServerType ServerType, const char* FunctionName);
TVector<SGeneratedRpcUnsupportedStat> GetGeneratedRpcUnsupportedStats();
TVector<SGeneratedRpcUnsupportedStat> GetGeneratedRpcUnsupportedStats(EServerType ServerType);
FString BuildGeneratedRpcUnsupportedStatsJson();
FString BuildGeneratedRpcUnsupportedStatsJson(EServerType ServerType);
uint16 GetClientDownlinkFunctionId(const char* FunctionName);
const char* GetClientDownlinkFunctionName(uint16 FunctionId);
bool BuildClientFunctionCallPacket(uint16 FunctionId, const TArray& InPayload, TArray& OutPacket);

template<typename TMessage>
inline bool BuildClientFunctionCallPacketForPayload(const char* FunctionName, const TMessage& Message, TArray& OutPacket)
{
    if (!FunctionName || FunctionName[0] == '\0')
    {
        return false;
    }

    return BuildClientFunctionCallPacket(
        GetClientDownlinkFunctionId(FunctionName),
        BuildPayload(Message),
        OutPacket);
}

template<typename... TArgs>
inline bool BuildRpcPayloadForRemoteCall(const char* ClassName, const char* FunctionName, TArray& OutData, TArgs&&... Args)
{
    if (!ClassName || !FunctionName)
    {
        return false;
    }

    if (MClass* Class = MReflectObject::FindClass(ClassName))
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
inline bool BuildRpcPayloadForEndpointCall(const SRpcEndpointBinding& Binding, TArray& OutData, TArgs&&... Args)
{
    return BuildRpcPayloadForRemoteCall(
        Binding.ClassName,
        Binding.FunctionName,
        OutData,
        std::forward<TArgs>(Args)...);
}

template<typename... TArgs>
inline bool BuildGeneratedRpcPayloadForServer(EServerType ServerType, const char* FunctionName, TArray& OutData, TArgs&&... Args)
{
    SRpcEndpointBinding Binding;
    if (!FindGeneratedRpcEndpoint(ServerType, FunctionName, Binding))
    {
        ReportUnsupportedGeneratedRpcEndpoint(ServerType, FunctionName);
        return false;
    }

    return BuildRpcPayloadForEndpointCall(Binding, OutData, std::forward<TArgs>(Args)...);
}

namespace MRpc
{
template<typename TRpcCall, typename TLegacyCall>
inline bool TryRpcOrLegacy(TRpcCall&& RpcCall, TLegacyCall&& LegacyCall)
{
    if (std::forward<TRpcCall>(RpcCall)())
    {
        return true;
    }

    std::forward<TLegacyCall>(LegacyCall)();
    return false;
}

template<typename TRpcCall, typename TConnection, typename TMessage>
inline bool TryRpcOrTypedLegacy(
    TRpcCall&& RpcCall,
    TConnection&& Connection,
    EServerMessageType LegacyType,
    const TMessage& LegacyMessage)
{
    return TryRpcOrLegacy(
        std::forward<TRpcCall>(RpcCall),
        [&]()
        {
            SendTypedServerMessage(std::forward<TConnection>(Connection), LegacyType, LegacyMessage);
        });
}

template<typename... TArgs>
inline bool Call(MServerConnection& Connection, EServerType ServerType, const char* FunctionName, TArgs&&... Args)
{
    TArray RpcPayload;
    if (!BuildGeneratedRpcPayloadForServer(ServerType, FunctionName, RpcPayload, std::forward<TArgs>(Args)...))
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
    TArray RpcPayload;
    if (!BuildGeneratedRpcPayloadForServer(ServerType, FunctionName, RpcPayload, std::forward<TArgs>(Args)...))
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
    TArray RpcPayload;
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
    TArray RpcPayload;
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
