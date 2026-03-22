#pragma once

#include "Protocol/ServerMessages.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Concurrency/Promise.h"
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

class IGeneratedClientResponseTarget
{
public:
    virtual ~IGeneratedClientResponseTarget() = default;
    virtual bool SendGeneratedClientResponse(uint64 ConnectionId, uint16 FunctionId, uint64 CallId, const TByteArray& Payload) = 0;
};

class IGeneratedServerCallResponseTarget
{
public:
    virtual ~IGeneratedServerCallResponseTarget() = default;
    virtual bool SendGeneratedServerCallResponse(uint16 FunctionId, uint64 CallId, bool bSuccess, const TByteArray& Payload) = 0;
};

struct SGeneratedClientCallContext
{
    uint64 ConnectionId = 0;
    uint16 FunctionId = 0;
    uint64 CallId = 0;
    IGeneratedClientResponseTarget* ResponseTarget = nullptr;

    bool IsValid() const
    {
        return ConnectionId != 0 && FunctionId != 0 && ResponseTarget != nullptr;
    }
};

struct SGeneratedServerCallContext
{
    uint16 FunctionId = 0;
    uint64 CallId = 0;
    IGeneratedServerCallResponseTarget* ResponseTarget = nullptr;

    bool IsValid() const
    {
        return FunctionId != 0 && CallId != 0 && ResponseTarget != nullptr;
    }
};

struct SGeneratedServerCallResponse
{
    bool bSuccess = false;
    TByteArray Payload;
};

class MGeneratedServerCallResponseTarget final : public IGeneratedServerCallResponseTarget
{
public:
    explicit MGeneratedServerCallResponseTarget(TFunction<bool(uint16, uint64, bool, const TByteArray&)> InSend)
        : SendCallback(std::move(InSend))
    {
    }

    bool SendGeneratedServerCallResponse(uint16 FunctionId, uint64 CallId, bool bSuccess, const TByteArray& Payload) override
    {
        return SendCallback ? SendCallback(FunctionId, CallId, bSuccess, Payload) : false;
    }

private:
    TFunction<bool(uint16, uint64, bool, const TByteArray&)> SendCallback;
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
EServerType ParseGeneratedServerTargetType(const char* TargetName);
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
    uint64 CallId,
    const TByteArray& Payload);
const MFunction* FindGeneratedClientFunctionById(const MClass* TargetClass, uint16 FunctionId);
const MFunction* FindGeneratedServerCallFunctionByName(const MClass* TargetClass, const char* FunctionName);
const MFunction* FindGeneratedServerCallFunctionById(const MClass* TargetClass, uint16 FunctionId);
uint64 GetCurrentClientConnectionId();
uint64 GetCurrentClientCallId();
SGeneratedClientCallContext CaptureCurrentClientCallContext();
void MarkCurrentClientCallDeferred();
bool IsCurrentClientCallDeferred();
bool SendDeferredClientCallResponse(const SGeneratedClientCallContext& Context, const TByteArray& Payload);
SGeneratedServerCallContext CaptureCurrentServerCallContext();
bool SendDeferredServerCallResponse(const SGeneratedServerCallContext& Context, bool bSuccess, const TByteArray& Payload);
bool SendDeferredServerCallSuccessResponse(const SGeneratedServerCallContext& Context, const TByteArray& Payload);
bool SendDeferredServerCallErrorResponse(const SGeneratedServerCallContext& Context, const FAppError& Error);
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
const MFunction* FindClientDownlinkFunctionById(uint16 FunctionId);
const MFunction* FindClientDownlinkFunctionByName(const char* FunctionName);
bool BuildClientFunctionPacket(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutPacket);
bool BuildClientCallPacket(uint16 FunctionId, uint64 CallId, const TByteArray& InPayload, TByteArray& OutPacket);
bool ParseClientFunctionPacket(const TByteArray& Data, uint16& OutFunctionId, uint32& OutPayloadSize, size_t& OutPayloadOffset);
bool ParseClientCallPacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, uint32& OutPayloadSize, size_t& OutPayloadOffset);
bool BuildServerCallPacket(uint16 FunctionId, uint64 CallId, const TByteArray& InPayload, TByteArray& OutPayload);
bool BuildServerCallResponsePacket(uint16 FunctionId, uint64 CallId, bool bSuccess, const TByteArray& InPayload, TByteArray& OutPayload);
bool ParseServerCallPacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, uint32& OutPayloadSize, size_t& OutPayloadOffset);
bool ParseServerCallResponsePacket(const TByteArray& Data, uint16& OutFunctionId, uint64& OutCallId, bool& OutSuccess, uint32& OutPayloadSize, size_t& OutPayloadOffset);
bool SendServerCallMessage(MServerConnection& Connection, const TByteArray& PacketPayload);
bool SendServerCallMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& PacketPayload);
bool SendServerCallMessage(INetConnection& Connection, const TByteArray& PacketPayload);
bool SendServerCallMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& PacketPayload);
bool SendServerCallResponseMessage(MServerConnection& Connection, const TByteArray& PacketPayload);
bool SendServerCallResponseMessage(const TSharedPtr<MServerConnection>& Connection, const TByteArray& PacketPayload);
bool SendServerCallResponseMessage(INetConnection& Connection, const TByteArray& PacketPayload);
bool SendServerCallResponseMessage(const TSharedPtr<INetConnection>& Connection, const TByteArray& PacketPayload);
bool DispatchGeneratedServerCall(
    MObject* TargetInstance,
    uint16 FunctionId,
    uint64 CallId,
    const TByteArray& Payload,
    IGeneratedServerCallResponseTarget& ResponseTarget);
bool HandleGeneratedServerCallResponse(const TByteArray& Data);
uint64 RegisterGeneratedServerCall(TFunction<void(const SGeneratedServerCallResponse&)> Completion);
bool ConsumeGeneratedServerCall(uint64 CallId, const SGeneratedServerCallResponse* Response);
bool BuildGeneratedServerCallPayload(const MFunction* Function, const TByteArray& RequestPayload, TByteArray& OutData);

template<typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallGeneratedServerFunction(
    TConnection&& Connection,
    const MClass* TargetClass,
    const char* FunctionName,
    const TRequest& Request);

template<typename TRequest>
inline bool BuildGeneratedServerCallPayload(const MFunction* Function, const TRequest& Request, TByteArray& OutData)
{
    if (!Function)
    {
        return false;
    }

    return BuildGeneratedServerCallPayload(Function, BuildPayload(Request), OutData);
}

template<typename TRequest>
inline bool BuildGeneratedServerCallPayloadByName(const MClass* TargetClass, const char* FunctionName, const TRequest& Request, TByteArray& OutData)
{
    const MFunction* Function = FindGeneratedServerCallFunctionByName(TargetClass, FunctionName);
    if (!Function)
    {
        return false;
    }

    return BuildGeneratedServerCallPayload(Function, Request, OutData);
}

template<typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallGeneratedServerFunction(
    TConnection&& Connection,
    const MClass* TargetClass,
    const char* FunctionName,
    const TRequest& Request)
{
    MPromise<TResult<TResponse, FAppError>> Promise;
    MFuture<TResult<TResponse, FAppError>> Future = Promise.GetFuture();

    const MFunction* Function = FindGeneratedServerCallFunctionByName(TargetClass, FunctionName);
    if (!Function)
    {
        Promise.SetValue(MakeErrorResult<TResponse>(FAppError::Make("server_call_missing_function", FunctionName ? FunctionName : "")));
        return Future;
    }

    TByteArray RequestPacket;
    if (!BuildGeneratedServerCallPayload(Function, Request, RequestPacket))
    {
        Promise.SetValue(MakeErrorResult<TResponse>(FAppError::Make("server_call_build_failed", Function->Name)));
        return Future;
    }

    MPromise<TResult<TResponse, FAppError>> CompletionPromise = Promise;
    const uint64 CallId = RegisterGeneratedServerCall(
        [Promise = std::move(CompletionPromise), FunctionName = MString(Function->Name)](const SGeneratedServerCallResponse& Response) mutable
        {
            if (Response.bSuccess)
            {
                TResponse ResponseValue {};
                auto ParseResult = ParsePayload(Response.Payload, ResponseValue, FunctionName.c_str());
                if (!ParseResult.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<TResponse>(FAppError::Make("server_call_response_parse_failed", ParseResult.GetError())));
                    return;
                }

                Promise.SetValue(TResult<TResponse, FAppError>::Ok(std::move(ResponseValue)));
                return;
            }

            FAppError Error;
            auto ParseResult = ParsePayload(Response.Payload, Error, FunctionName.c_str());
            if (!ParseResult.IsOk())
            {
                Promise.SetValue(MakeErrorResult<TResponse>(FAppError::Make("server_call_error_parse_failed", ParseResult.GetError())));
                return;
            }

            Promise.SetValue(MakeErrorResult<TResponse>(std::move(Error)));
        });

    if (CallId == 0)
    {
        Promise.SetValue(MakeErrorResult<TResponse>(FAppError::Make("server_call_register_failed", Function->Name)));
        return Future;
    }

    TByteArray PacketPayload;
    if (!BuildServerCallPacket(Function->FunctionId, CallId, RequestPacket, PacketPayload))
    {
        ConsumeGeneratedServerCall(CallId, nullptr);
        Promise.SetValue(MakeErrorResult<TResponse>(FAppError::Make("server_call_packet_build_failed", Function->Name)));
        return Future;
    }

    if (!SendServerCallMessage(std::forward<TConnection>(Connection), PacketPayload))
    {
        ConsumeGeneratedServerCall(CallId, nullptr);
        Promise.SetValue(MakeErrorResult<TResponse>(FAppError::Make("server_call_send_failed", Function->Name)));
    }

    return Future;
}

inline bool BuildClientFunctionArgsPayload(const MFunction* Function, TByteArray& OutPayload)
{
    if (!Function)
    {
        return false;
    }

    MReflectArchive Ar;
    if (!SerializeFunctionArgsByMeta(Function, Ar))
    {
        return false;
    }

    OutPayload = std::move(Ar.Data);
    return true;
}

template<typename... TArgs>
inline bool BuildClientFunctionArgsPayload(const MFunction* Function, TByteArray& OutPayload, TArgs&&... Args)
{
    if (!Function)
    {
        return false;
    }

    MReflectArchive Ar;
    if (!SerializeFunctionArgsByMeta(Function, Ar, std::forward<TArgs>(Args)...))
    {
        return false;
    }

    OutPayload = std::move(Ar.Data);
    return true;
}

template<typename... TArgs>
inline bool BuildClientFunctionCallPacketById(uint16 FunctionId, TByteArray& OutPacket, TArgs&&... Args)
{
    const MFunction* Function = FindClientDownlinkFunctionById(FunctionId);
    TByteArray Payload;
    if (!BuildClientFunctionArgsPayload(Function, Payload, std::forward<TArgs>(Args)...))
    {
        return false;
    }

    return BuildClientFunctionPacket(FunctionId, Payload, OutPacket);
}

template<typename... TArgs>
inline bool BuildClientFunctionCallPacketByName(const char* FunctionName, TByteArray& OutPacket, TArgs&&... Args)
{
    const MFunction* Function = FindClientDownlinkFunctionByName(FunctionName);
    if (!Function)
    {
        return false;
    }

    return BuildClientFunctionCallPacketById(Function->FunctionId, OutPacket, std::forward<TArgs>(Args)...);
}

template<typename TResponse>
inline bool SendDeferredClientCallResponse(const SGeneratedClientCallContext& Context, const TResponse& Response)
{
    if (!Context.IsValid())
    {
        return false;
    }

    const TByteArray Payload = BuildPayload(Response);
    return SendDeferredClientCallResponse(Context, Payload);
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
