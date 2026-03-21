#pragma once

#include "Protocol/ServerMessages.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/MLib.h"
#include "Common/Net/NetMessages.h"

#include <cstddef>
#include <utility>

bool BuildServerRpcPayload(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutData);
bool BuildServerRpcMessage(const TByteArray& RpcPayload, TByteArray& OutPacket);
bool SendServerRpcMessage(MServerConnection& Connection, const TByteArray& RpcPayload);
bool SendServerRpcMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& RpcPayload);
bool SendServerRpcMessage(INetConnection& Connection, const TByteArray& RpcPayload);
bool SendServerRpcMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& RpcPayload);

struct SRpcEndpointBinding
{
    EServerType ServerType = EServerType::Unknown;
    const char* ClassName = nullptr;
    const char* FunctionName = nullptr;
};

struct SGeneratedRpcUnsupportedStat
{
    EServerType ServerType = EServerType::Unknown;
    MString FunctionName;
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
    const TByteArray* Payload = nullptr;
};

namespace MClientDownlink
{
inline constexpr const char* ScopeName = "MClientDownlink";
inline constexpr const char* OnLoginResponse = "Client_OnLoginResponse";
inline constexpr const char* OnActorCreate = "Client_OnActorCreate";
inline constexpr const char* OnActorUpdate = "Client_OnActorUpdate";
inline constexpr const char* OnActorDestroy = "Client_OnActorDestroy";
inline constexpr const char* OnInventoryPull = "Client_OnInventoryPull";

inline uint16 Id_OnLoginResponse()
{
    static uint16 Id = MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_OnLoginResponse");
    return Id;
}

inline uint16 Id_OnActorCreate()
{
    static uint16 Id = MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_OnActorCreate");
    return Id;
}

inline uint16 Id_OnActorUpdate()
{
    static uint16 Id = MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_OnActorUpdate");
    return Id;
}

inline uint16 Id_OnActorDestroy()
{
    static uint16 Id = MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_OnActorDestroy");
    return Id;
}

inline uint16 Id_OnInventoryPull()
{
    static uint16 Id = MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_OnInventoryPull");
    return Id;
}
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

bool BuildRpcPayloadForEndpoint(const SRpcEndpointBinding& Binding, const TByteArray& InPayload, TByteArray& OutData);
bool TryInvokeServerRpc(MObject* ServiceInstance, const TByteArray& Data, ERpcType ExpectedType);
bool TryInvokeServerRpc(MObject* ServiceInstance, uint64 ConnectionId, const TByteArray& Data, ERpcType ExpectedType);
uint16 PeekServerRpcFunctionId(const TByteArray& Data);
uint64 GetCurrentServerRpcConnectionId();
bool TryDispatchGeneratedClientMessage(
    MObject* TargetInstance,
    uint64 ConnectionId,
    EClientMessageType MessageType,
    const TByteArray& Payload);
SGeneratedClientDispatchOutcome DispatchGeneratedClientMessage(
    MObject* TargetInstance,
    uint64 ConnectionId,
    EClientMessageType MessageType,
    const TByteArray& Payload);
SGeneratedClientDispatchOutcome DispatchGeneratedClientFunction(
    MObject* TargetInstance,
    uint64 ConnectionId,
    uint16 FunctionId,
    const TByteArray& Payload);
const char* GetServerTypeDisplayName(EServerType ServerType);
inline const char* GetServerEndpointClassName(EServerType ServerType)
{
    switch (ServerType)
    {
    case EServerType::Gateway:
        return "MGatewayServer";
    case EServerType::Login:
        return "MLoginServer";
    case EServerType::World:
        return "MWorldServer";
    case EServerType::Scene:
        return "MSceneServer";
    case EServerType::Router:
        return "MRouterServer";
    case EServerType::Mgo:
        return "MMgoServer";
    default:
        return nullptr;
    }
}
bool FindGeneratedRpcEndpoint(EServerType ServerType, const char* FunctionName, SRpcEndpointBinding& OutBinding);
bool ServerSupportsGeneratedRpc(EServerType ServerType, const char* FunctionName);
size_t GetGeneratedRpcEntryCount();
TVector<MString> GetGeneratedRpcFunctionNames(EServerType ServerType);
MString BuildGeneratedRpcManifestJson(EServerType ServerType);
void ReportUnsupportedGeneratedRpcEndpoint(EServerType ServerType, const char* FunctionName);
TVector<SGeneratedRpcUnsupportedStat> GetGeneratedRpcUnsupportedStats();
TVector<SGeneratedRpcUnsupportedStat> GetGeneratedRpcUnsupportedStats(EServerType ServerType);
MString BuildGeneratedRpcUnsupportedStatsJson();
MString BuildGeneratedRpcUnsupportedStatsJson(EServerType ServerType);
uint16 GetClientDownlinkFunctionId(const char* FunctionName);
const char* GetClientDownlinkFunctionName(uint16 FunctionId);
bool BuildClientFunctionCallPacket(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutPacket);

template<typename TMessage>
inline bool BuildClientFunctionCallPacketForPayload(const char* FunctionName, const TMessage& Message, TByteArray& OutPacket)
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

template<typename TMessage>
inline bool BuildClientFunctionCallPacketForPayload(uint16 FunctionId, const TMessage& Message, TByteArray& OutPacket)
{
    return BuildClientFunctionCallPacket(FunctionId, BuildPayload(Message), OutPacket);
}

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
inline bool BuildGeneratedRpcPayloadForServer(EServerType ServerType, const char* FunctionName, TByteArray& OutData, TArgs&&... Args)
{
    SRpcEndpointBinding Binding;
    if (!FindGeneratedRpcEndpoint(ServerType, FunctionName, Binding))
    {
        const char* ClassName = GetServerEndpointClassName(ServerType);
        if (!ClassName || !FunctionName)
        {
            ReportUnsupportedGeneratedRpcEndpoint(ServerType, FunctionName);
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
    TByteArray RpcPayload;
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
