#include "NetDriver/ServerRpcServices.h"
#include "Common/Logger.h"

namespace
{
const SRpcEndpointBinding GRouterServerRegisterAckEndpoints[] = {
    MGATEWAY_SERVER_ROUTER_ACK_RPC_LIST(MDECLARE_RPC_ENDPOINT_BINDING)
    MLOGIN_SERVER_ROUTER_ACK_RPC_LIST(MDECLARE_RPC_ENDPOINT_BINDING)
    MWORLD_SERVER_ROUTER_ACK_RPC_LIST(MDECLARE_RPC_ENDPOINT_BINDING)
};

const SRpcEndpointBinding GRouterRouteResponseEndpoints[] = {
    MGATEWAY_SERVER_ROUTER_ROUTE_RPC_LIST(MDECLARE_RPC_ENDPOINT_BINDING)
    MWORLD_SERVER_ROUTER_ROUTE_RPC_LIST(MDECLARE_RPC_ENDPOINT_BINDING)
};

const SRpcEndpointBinding* FindEndpointByServerType(
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

#define MDEFINE_SERVICE_RPC_HANDLER_STORAGE(ServiceClass, MethodName) \
    ServiceClass::FHandler_##MethodName G##ServiceClass##_##MethodName##Handler; \
    uint16 G##ServiceClass##_##MethodName##FunctionId = 0;

MDEFINE_SERVICE_RPC_HANDLER_STORAGE(MGatewayService, Rpc_OnPlayerLoginResponse)
MDEFINE_SERVICE_RPC_HANDLER_STORAGE(MLoginService, Rpc_OnPlayerLoginRequest)
MDEFINE_SERVICE_RPC_HANDLER_STORAGE(MLoginService, Rpc_OnSessionValidateRequest)
MDEFINE_SERVICE_RPC_HANDLER_STORAGE(MWorldService, Rpc_OnPlayerLoginRequest)
MDEFINE_SERVICE_RPC_HANDLER_STORAGE(MWorldService, Rpc_OnSessionValidateResponse)

#undef MDEFINE_SERVICE_RPC_HANDLER_STORAGE
} // namespace

#define MFORWARD_SERVER_RPC_TO_HANDLER(HandlerVar, LogFormat, ...) \
    do \
    { \
        if (HandlerVar) \
        { \
            HandlerVar(__VA_ARGS__); \
        } \
        else \
        { \
            LOG_WARN(LogFormat, __VA_ARGS__); \
        } \
    } while (0)

#define MDEFINE_SERVICE_RPC_HANDLER_API(ServiceClass, MethodName) \
    void ServiceClass::SetHandler_##MethodName(const FHandler_##MethodName& InHandler) \
    { \
        G##ServiceClass##_##MethodName##Handler = InHandler; \
    } \
    \
    uint16 ServiceClass::GetFunctionId_##MethodName() \
    { \
        return MGET_CACHED_RPC_FUNCTION_ID( \
            ServiceClass, \
            MethodName, \
            G##ServiceClass##_##MethodName##FunctionId); \
    }

#define MREGISTER_SERVICE_RPC(MethodName, FuncFlags, RpcKind, ReliableValue) \
    MREGISTER_RPC_METHOD(MethodName, FuncFlags, RpcKind, ReliableValue)

#define MDEFINE_SERVICE_RPC_METHOD(ServiceClass, MethodName, HandlerVar, LogFormat, ...) \
    void ServiceClass::MethodName(__VA_ARGS__) \
    { \
        MFORWARD_SERVER_RPC_TO_HANDLER(HandlerVar, LogFormat, __VA_ARGS__); \
    } \
    MDEFINE_SERVICE_RPC_HANDLER_API(ServiceClass, MethodName)

bool BuildServerRpcPayload(uint16 FunctionId, const TArray& InPayload, TArray& OutData)
{
    const uint32 PayloadSize = static_cast<uint32>(InPayload.size());

    OutData.clear();
    OutData.reserve(sizeof(FunctionId) + sizeof(PayloadSize) + PayloadSize);

    const uint8* FuncPtr = reinterpret_cast<const uint8*>(&FunctionId);
    OutData.insert(OutData.end(), FuncPtr, FuncPtr + sizeof(FunctionId));

    const uint8* SizePtr = reinterpret_cast<const uint8*>(&PayloadSize);
    OutData.insert(OutData.end(), SizePtr, SizePtr + sizeof(PayloadSize));

    if (PayloadSize > 0)
    {
        OutData.insert(OutData.end(), InPayload.begin(), InPayload.end());
    }

    return true;
}

bool BuildRpcPayloadForEndpoint(const SRpcEndpointBinding& Binding, const TArray& InPayload, TArray& OutData)
{
    if (!Binding.ClassName || !Binding.FunctionName)
    {
        return false;
    }

    const uint16 FunctionId = MGET_STABLE_RPC_FUNCTION_ID(Binding.ClassName, Binding.FunctionName);
    if (FunctionId == 0)
    {
        return false;
    }

    return BuildServerRpcPayload(FunctionId, InPayload, OutData);
}

const SRpcEndpointBinding* FindRouterServerRegisterAckEndpoint(EServerType ServerType)
{
    return FindEndpointByServerType(
        GRouterServerRegisterAckEndpoints,
        sizeof(GRouterServerRegisterAckEndpoints) / sizeof(GRouterServerRegisterAckEndpoints[0]),
        ServerType);
}

const SRpcEndpointBinding* FindRouterRouteResponseEndpoint(EServerType ServerType)
{
    return FindEndpointByServerType(
        GRouterRouteResponseEndpoints,
        sizeof(GRouterRouteResponseEndpoints) / sizeof(GRouterRouteResponseEndpoints[0]),
        ServerType);
}

bool TryInvokeServerRpc(MReflectObject* ServiceInstance, const TArray& Data, ERpcType ExpectedType)
{
    if (!ServiceInstance)
    {
        return false;
    }

    if (Data.size() < sizeof(uint16) + sizeof(uint32))
    {
        LOG_WARN("TryInvokeServerRpc: packet too small (size=%llu)",
                 static_cast<unsigned long long>(Data.size()));
        return false;
    }

    size_t Offset = 0;
    uint16 FunctionId = 0;
    uint32 PayloadSize = 0;

    std::memcpy(&FunctionId, Data.data() + Offset, sizeof(FunctionId));
    Offset += sizeof(FunctionId);

    std::memcpy(&PayloadSize, Data.data() + Offset, sizeof(PayloadSize));
    Offset += sizeof(PayloadSize);

    if (Offset + PayloadSize > Data.size())
    {
        LOG_WARN("TryInvokeServerRpc: payload out of range (size=%llu, payload=%u)",
                 static_cast<unsigned long long>(Data.size()),
                 static_cast<unsigned>(PayloadSize));
        return false;
    }

    MClass* ServiceClass = ServiceInstance->GetClass();
    if (!ServiceClass)
    {
        LOG_ERROR("TryInvokeServerRpc: ServiceInstance has no class");
        return false;
    }

    MFunction* FuncMeta = ServiceClass->FindFunctionById(FunctionId);
    if (!FuncMeta || !FuncMeta->RpcFunc)
    {
        LOG_WARN("TryInvokeServerRpc: unknown FunctionId=%u",
                 static_cast<unsigned>(FunctionId));
        return false;
    }

    if (FuncMeta->RpcType != ExpectedType)
    {
        LOG_WARN("TryInvokeServerRpc: RpcType mismatch for FunctionId=%u (expected=%d, actual=%d)",
                 static_cast<unsigned>(FunctionId),
                 static_cast<int>(ExpectedType),
                 static_cast<int>(FuncMeta->RpcType));
        return false;
    }

    TArray Payload;
    if (PayloadSize > 0)
    {
        Payload.resize(PayloadSize);
        std::memcpy(Payload.data(), Data.data() + Offset, PayloadSize);
    }

    MReflectArchive Ar(Payload);
    FuncMeta->RpcFunc(ServiceInstance, Ar);
    return true;
}

void MGatewayService::Rpc_OnPlayerLoginResponse(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)
{
    MFORWARD_SERVER_RPC_TO_HANDLER(
        GMGatewayService_Rpc_OnPlayerLoginResponseHandler,
        "MGatewayService Rpc_OnPlayerLoginResponse with no handler bound (ClientConnId=%llu, PlayerId=%llu, SessionKey=%u)",
        ClientConnectionId,
        PlayerId,
        SessionKey);
}

MDEFINE_SERVICE_RPC_HANDLER_API(MGatewayService, Rpc_OnPlayerLoginResponse)

void MLoginService::Rpc_OnSessionValidateRequest(uint64 ValidationRequestId, uint64 PlayerId, uint32 SessionKey)
{
    MFORWARD_SERVER_RPC_TO_HANDLER(
        GMLoginService_Rpc_OnSessionValidateRequestHandler,
        "MLoginService Rpc_OnSessionValidateRequest with no handler bound (ValidationRequestId=%llu, PlayerId=%llu, SessionKey=%u)",
        ValidationRequestId,
        PlayerId,
        SessionKey);
}

void MLoginService::Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId)
{
    MFORWARD_SERVER_RPC_TO_HANDLER(
        GMLoginService_Rpc_OnPlayerLoginRequestHandler,
        "MLoginService Rpc_OnPlayerLoginRequest with no handler bound (ClientConnId=%llu, PlayerId=%llu)",
        ClientConnectionId,
        PlayerId);
}

MDEFINE_SERVICE_RPC_HANDLER_API(MLoginService, Rpc_OnPlayerLoginRequest)
MDEFINE_SERVICE_RPC_HANDLER_API(MLoginService, Rpc_OnSessionValidateRequest)

void MWorldService::Rpc_OnSessionValidateResponse(uint64 ValidationRequestId, uint64 PlayerId, bool bValid)
{
    MFORWARD_SERVER_RPC_TO_HANDLER(
        GMWorldService_Rpc_OnSessionValidateResponseHandler,
        "MWorldService Rpc_OnSessionValidateResponse with no handler bound (ValidationRequestId=%llu, PlayerId=%llu, bValid=%d)",
        ValidationRequestId,
        PlayerId,
        bValid);
}

void MWorldService::Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)
{
    MFORWARD_SERVER_RPC_TO_HANDLER(
        GMWorldService_Rpc_OnPlayerLoginRequestHandler,
        "MWorldService Rpc_OnPlayerLoginRequest with no handler bound (ClientConnId=%llu, PlayerId=%llu, SessionKey=%u)",
        ClientConnectionId,
        PlayerId,
        SessionKey);
}

MDEFINE_SERVICE_RPC_HANDLER_API(MWorldService, Rpc_OnPlayerLoginRequest)
MDEFINE_SERVICE_RPC_HANDLER_API(MWorldService, Rpc_OnSessionValidateResponse)

#undef MDEFINE_SERVICE_RPC_HANDLER_API
#undef MREGISTER_SERVICE_RPC
#undef MDEFINE_SERVICE_RPC_METHOD
#undef MFORWARD_SERVER_RPC_TO_HANDLER
