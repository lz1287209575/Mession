#include "Servers/World/Services/WorldClientServiceEndpoint.h"
#include "Servers/World/Rpc/WorldBackendRpc.h"
#include "Servers/World/Services/WorldPlayerServiceEndpoint.h"

#include <cstring>
#include <typeindex>

namespace
{
template<typename TResponse>
TResponse BuildFailureResponse(const FAppError& Error, const char* FallbackCode)
{
    TResponse Failed;
    Failed.Error = Error.Code.empty() ? (FallbackCode ? FallbackCode : "client_call_failed") : Error.Code;
    return Failed;
}

template<typename TMethod>
struct TPlayerMethodTraits;

template<typename TObject, typename TResponse, typename TRequest>
struct TPlayerMethodTraits<MFuture<TResult<TResponse, FAppError>>(TObject::*)(const TRequest&)>
{
    using TPlayerRequest = TRequest;
    using TPlayerResponse = TResponse;
};

namespace MClientBindingDetail
{
template<typename TStruct>
MClass* FindStructClass()
{
    return MObject::FindStruct(std::type_index(typeid(TStruct)));
}

inline bool AreCompatibleProperties(const MProperty* DestProperty, const MProperty* SrcProperty)
{
    if (!DestProperty || !SrcProperty)
    {
        return false;
    }

    if (DestProperty->Type != SrcProperty->Type || DestProperty->Size != SrcProperty->Size)
    {
        return false;
    }

    switch (DestProperty->Type)
    {
    case EPropertyType::Struct:
    case EPropertyType::Enum:
        return DestProperty->CppTypeIndex == SrcProperty->CppTypeIndex;
    default:
        return true;
    }
}

inline bool CopyPropertyValue(
    const MProperty* DestProperty,
    void* DestObject,
    const MProperty* SrcProperty,
    const void* SrcObject);

inline bool CopyStructValue(
    const MProperty* DestProperty,
    void* DestObject,
    const MProperty* SrcProperty,
    const void* SrcObject)
{
    if (!AreCompatibleProperties(DestProperty, SrcProperty))
    {
        return false;
    }

    void* DestValue = DestProperty->GetValueVoidPtr(DestObject);
    const void* SrcValue = SrcProperty->GetValueVoidPtr(SrcObject);
    if (!DestValue || !SrcValue)
    {
        return false;
    }

    MClass* StructClass = MObject::FindStruct(DestProperty->CppTypeIndex);
    if (!StructClass)
    {
        return false;
    }

    for (const MProperty* ChildDestProperty : StructClass->GetProperties())
    {
        if (!ChildDestProperty)
        {
            continue;
        }

        const MProperty* ChildSrcProperty = StructClass->FindProperty(ChildDestProperty->Name);
        if (!ChildSrcProperty)
        {
            continue;
        }

        (void)CopyPropertyValue(ChildDestProperty, DestValue, ChildSrcProperty, SrcValue);
    }

    return true;
}

inline bool CopyPropertyValue(
    const MProperty* DestProperty,
    void* DestObject,
    const MProperty* SrcProperty,
    const void* SrcObject)
{
    if (!AreCompatibleProperties(DestProperty, SrcProperty))
    {
        return false;
    }

    void* DestValue = DestProperty->GetValueVoidPtr(DestObject);
    const void* SrcValue = SrcProperty->GetValueVoidPtr(SrcObject);
    if (!DestValue || !SrcValue)
    {
        return false;
    }

    switch (DestProperty->Type)
    {
    case EPropertyType::Int8:
        *static_cast<int8*>(DestValue) = *static_cast<const int8*>(SrcValue);
        return true;
    case EPropertyType::Int16:
        *static_cast<int16*>(DestValue) = *static_cast<const int16*>(SrcValue);
        return true;
    case EPropertyType::Int32:
        *static_cast<int32*>(DestValue) = *static_cast<const int32*>(SrcValue);
        return true;
    case EPropertyType::Int64:
        *static_cast<int64*>(DestValue) = *static_cast<const int64*>(SrcValue);
        return true;
    case EPropertyType::UInt8:
        *static_cast<uint8*>(DestValue) = *static_cast<const uint8*>(SrcValue);
        return true;
    case EPropertyType::UInt16:
        *static_cast<uint16*>(DestValue) = *static_cast<const uint16*>(SrcValue);
        return true;
    case EPropertyType::UInt32:
        *static_cast<uint32*>(DestValue) = *static_cast<const uint32*>(SrcValue);
        return true;
    case EPropertyType::UInt64:
        *static_cast<uint64*>(DestValue) = *static_cast<const uint64*>(SrcValue);
        return true;
    case EPropertyType::Float:
        *static_cast<float*>(DestValue) = *static_cast<const float*>(SrcValue);
        return true;
    case EPropertyType::Double:
        *static_cast<double*>(DestValue) = *static_cast<const double*>(SrcValue);
        return true;
    case EPropertyType::Bool:
        *static_cast<bool*>(DestValue) = *static_cast<const bool*>(SrcValue);
        return true;
    case EPropertyType::String:
    case EPropertyType::Name:
        *static_cast<MString*>(DestValue) = *static_cast<const MString*>(SrcValue);
        return true;
    case EPropertyType::Vector:
        *static_cast<SVector*>(DestValue) = *static_cast<const SVector*>(SrcValue);
        return true;
    case EPropertyType::Rotator:
        *static_cast<SRotator*>(DestValue) = *static_cast<const SRotator*>(SrcValue);
        return true;
    case EPropertyType::Enum:
        std::memcpy(DestValue, SrcValue, DestProperty->Size);
        return true;
    case EPropertyType::Struct:
        return CopyStructValue(DestProperty, DestObject, SrcProperty, SrcObject);
    default:
        return false;
    }
}

template<typename TDest, typename TSrc>
void CopyMatchingProperties(TDest& Dest, const TSrc& Src)
{
    MClass* DestClass = FindStructClass<TDest>();
    MClass* SrcClass = FindStructClass<TSrc>();
    if (!DestClass || !SrcClass)
    {
        return;
    }

    for (const MProperty* DestProperty : DestClass->GetProperties())
    {
        if (!DestProperty)
        {
            continue;
        }

        const MProperty* SrcProperty = SrcClass->FindProperty(DestProperty->Name);
        if (!SrcProperty)
        {
            continue;
        }

        (void)CopyPropertyValue(DestProperty, &Dest, SrcProperty, &Src);
    }
}

template<typename TResponse>
void MarkSuccessIfPresent(TResponse& Response)
{
    MClass* ResponseClass = FindStructClass<TResponse>();
    if (!ResponseClass)
    {
        return;
    }

    const MProperty* SuccessProperty = ResponseClass->FindProperty("bSuccess");
    if (!SuccessProperty || SuccessProperty->Type != EPropertyType::Bool)
    {
        return;
    }

    void* Value = SuccessProperty->GetValueVoidPtr(&Response);
    if (!Value)
    {
        return;
    }

    *static_cast<bool*>(Value) = true;
}
} // namespace MClientBindingDetail

}

namespace MWorldClientDetail
{
class FClientPlayerCallBinding
{
public:
    explicit FClientPlayerCallBinding(MWorldPlayerServiceEndpoint* InPlayerService)
        : PlayerService(InPlayerService)
    {
    }

#define M_WORLD_CLIENT_PLAYER_ROUTE(MethodName, PlayerMethodName, ClientRequestType, ClientResponseType, FailureCode) \
    void MethodName(ClientRequestType& Request, ClientResponseType& Response) const \
    { \
        Dispatch<&MWorldPlayerServiceEndpoint::PlayerMethodName>(FailureCode, Request, Response); \
    }
#include "Servers/World/Services/WorldClientPlayerRouteList.inl"
#undef M_WORLD_CLIENT_PLAYER_ROUTE

private:
    template<auto TPlayerMethod, typename TClientRequest, typename TClientResponse>
    void Dispatch(
        const char* FailureCode,
        const TClientRequest& Request,
        TClientResponse& Response) const
    {
        using TMethodTraits = TPlayerMethodTraits<decltype(TPlayerMethod)>;
        using TPlayerRequest = typename TMethodTraits::TPlayerRequest;
        using TPlayerResponse = typename TMethodTraits::TPlayerResponse;

        if (Request.PlayerId == 0)
        {
            Response.Error = "player_id_required";
            return;
        }

        if (!PlayerService)
        {
            Response.Error = "world_player_service_missing";
            return;
        }

        const SClientCallContext Context = CaptureCurrentClientCallContext();
        if (!Context.IsValid())
        {
            Response.Error = "client_call_context_missing";
            return;
        }

        TPlayerRequest PlayerRequest {};
        MClientBindingDetail::CopyMatchingProperties(PlayerRequest, Request);

        (void)MClientCallAsyncSupport::StartDeferred<TClientResponse>(
            Context,
            MClientCallAsyncSupport::Map(
                (PlayerService->*TPlayerMethod)(PlayerRequest),
                [](const TPlayerResponse& PlayerResponse)
                {
                    TClientResponse ClientResponse {};
                    MClientBindingDetail::CopyMatchingProperties(ClientResponse, PlayerResponse);
                    MClientBindingDetail::MarkSuccessIfPresent(ClientResponse);
                    return ClientResponse;
                }),
            [FailureCode](const FAppError& Error)
            {
                return BuildFailureResponse<TClientResponse>(Error, FailureCode);
            });
    }

private:
    MWorldPlayerServiceEndpoint* PlayerService = nullptr;
};
} // namespace MWorldClientDetail

namespace MWorldClientFlows
{
class FClientLoginWorkflow final
    : public MClientCallAsyncSupport::TClientCallWorkflow<FClientLoginWorkflow, FClientLoginResponse>
{
public:
    using TResponseType = FClientLoginResponse;

    FClientLoginWorkflow(
        MWorldLoginRpc* InLoginRpc,
        MWorldPlayerServiceEndpoint* InPlayerService,
        FClientLoginRequest InRequest,
        uint64 InGatewayConnectionId)
        : LoginRpc(InLoginRpc)
        , PlayerService(InPlayerService)
        , Request(std::move(InRequest))
        , GatewayConnectionId(InGatewayConnectionId)
    {
    }

protected:
    void OnStart() override
    {
        if (!LoginRpc || !PlayerService)
        {
            Fail("client_login_dependencies_missing", "Client_Login");
            return;
        }

        FLoginIssueSessionRequest IssueRequest;
        IssueRequest.PlayerId = Request.PlayerId;
        IssueRequest.GatewayConnectionId = GatewayConnectionId;
        Continue(LoginRpc->IssueSession(IssueRequest), &FClientLoginWorkflow::OnSessionIssued);
    }

private:
    void OnSessionIssued(const FLoginIssueSessionResponse& LoginResponse)
    {
        SessionKey = LoginResponse.SessionKey;

        FPlayerEnterWorldRequest EnterRequest;
        EnterRequest.PlayerId = Request.PlayerId;
        EnterRequest.GatewayConnectionId = GatewayConnectionId;
        EnterRequest.SessionKey = SessionKey;
        Continue(PlayerService->PlayerEnterWorld(EnterRequest), &FClientLoginWorkflow::OnPlayerEnteredWorld);
    }

    void OnPlayerEnteredWorld(const FPlayerEnterWorldResponse&)
    {
        FClientLoginResponse Response;
        Response.bSuccess = true;
        Response.PlayerId = Request.PlayerId;
        Response.SessionKey = SessionKey;
        Succeed(std::move(Response));
    }

    MWorldLoginRpc* LoginRpc = nullptr;
    MWorldPlayerServiceEndpoint* PlayerService = nullptr;
    FClientLoginRequest Request;
    uint64 GatewayConnectionId = 0;
    uint32 SessionKey = 0;
};
}

void MWorldClientServiceEndpoint::Initialize(
    MWorldPlayerServiceEndpoint* InPlayerService,
    MWorldLoginRpc* InLoginRpc)
{
    PlayerService = InPlayerService;
    LoginRpc = InLoginRpc;
}

void MWorldClientServiceEndpoint::Client_Login(FClientLoginRequest& Request, FClientLoginResponse& Response)
{
    const uint64 GatewayConnectionId = GetCurrentClientConnectionId();
    if (GatewayConnectionId == 0)
    {
        Response.Error = "client_context_missing";
        return;
    }

    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    (void)MClientCallAsyncSupport::StartDeferred<FClientLoginResponse>(
        Context,
        StartClientLoginFlow(Request, GatewayConnectionId),
        [](const FAppError& Error)
        {
            return BuildFailureResponse<FClientLoginResponse>(Error, "client_login_failed");
        });
}

#define M_WORLD_CLIENT_PLAYER_ROUTE(MethodName, PlayerMethodName, ClientRequestType, ClientResponseType, FailureCode) \
void MWorldClientServiceEndpoint::Client_##MethodName(ClientRequestType& Request, ClientResponseType& Response) \
{ \
    ClientPlayerCall().MethodName(Request, Response); \
}
#include "Servers/World/Services/WorldClientPlayerRouteList.inl"
#undef M_WORLD_CLIENT_PLAYER_ROUTE

MWorldClientDetail::FClientPlayerCallBinding MWorldClientServiceEndpoint::ClientPlayerCall() const
{
    return MWorldClientDetail::FClientPlayerCallBinding(PlayerService);
}

MFuture<TResult<FClientLoginResponse, FAppError>> MWorldClientServiceEndpoint::StartClientLoginFlow(
    const FClientLoginRequest& Request,
    uint64 GatewayConnectionId)
{
    if (Request.PlayerId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("player_id_required", "Client_Login");
    }

    if (GatewayConnectionId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("gateway_connection_id_required", "Client_Login");
    }

    if (!LoginRpc || !LoginRpc->IsAvailable())
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("login_server_unavailable", "Client_Login");
    }

    if (!PlayerService)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("world_player_service_missing", "Client_Login");
    }

    return MClientCallAsyncSupport::StartWorkflow<MWorldClientFlows::FClientLoginWorkflow>(
        LoginRpc,
        PlayerService,
        Request,
        GatewayConnectionId);
}
