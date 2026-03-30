#include "Servers/World/Services/WorldPlayerServiceEndpoint.h"
#include "Common/Runtime/Persistence/PersistenceSubsystem.h"
#include "Common/Runtime/StringUtils.h"
#include "Protocol/Messages/Auth/AuthSessionMessages.h"
#include "Protocol/Messages/Mgo/MgoPlayerStateMessages.h"
#include "Protocol/Messages/Router/RouterServiceMessages.h"
#include "Servers/World/Players/Player.h"

namespace MWorldPlayerServiceFlows
{
namespace
{
template<typename TResponse>
TResponse BuildPlayerOnlyResponse(uint64 PlayerId)
{
    TResponse Response;
    Response.PlayerId = PlayerId;
    return Response;
}

FObjectPersistenceRecord ToProtocolPersistenceRecord(const SPersistenceRecord& Record)
{
    return FObjectPersistenceRecord{
        Record.ObjectPath,
        Record.ClassName,
        Record.SnapshotData,
    };
}

TVector<FObjectPersistenceRecord> ToProtocolPersistenceRecords(const TVector<SPersistenceRecord>& Records)
{
    TVector<FObjectPersistenceRecord> Result;
    Result.reserve(Records.size());
    for (const SPersistenceRecord& Record : Records)
    {
        Result.push_back(ToProtocolPersistenceRecord(Record));
    }
    return Result;
}

TVector<SObjectDomainSnapshotRecord> FilterCompatiblePlayerRecords(const TVector<FObjectPersistenceRecord>& Records)
{
    TVector<SObjectDomainSnapshotRecord> Result;
    Result.reserve(Records.size());
    for (const FObjectPersistenceRecord& Record : Records)
    {
        // Backward compatibility:
        // 1. Old saves used MPlayerSession as the persistence root.
        // 2. The first Player refactor used MPlayer as root with route fields on the root object.
        // 3. Avatar was renamed to Profile in the current split.
        if (Record.ObjectPath.empty() &&
            (Record.ClassName == "MPlayerSession" || Record.ClassName == "MPlayer"))
        {
            continue;
        }

        MString ObjectPath = Record.ObjectPath;
        MString ClassName = Record.ClassName;

        if (!ObjectPath.empty())
        {
            TStringView PathView(ObjectPath);
            if (MStringView::StartsWith(PathView, "Avatar"))
            {
                ObjectPath.replace(0, sizeof("Avatar") - 1, "Profile");
            }
        }

        if (ClassName == "MPlayerAvatar")
        {
            ClassName = "MPlayerProfile";
        }
        else if (ClassName == "MInventoryComponent")
        {
            ClassName = "MPlayerInventory";
        }
        else if (ClassName == "MAttributeComponent")
        {
            ClassName = "MPlayerProgression";
        }

        if (!ObjectPath.empty())
        {
            TStringView PathView(ObjectPath);
            if (MStringView::StartsWith(PathView, "Profile.Attributes"))
            {
                ObjectPath.replace(0, sizeof("Profile.Attributes") - 1, "Profile.Progression");
            }
        }

        Result.push_back(SObjectDomainSnapshotRecord{
            0,
            0,
            0,
            std::move(ObjectPath),
            std::move(ClassName),
            Record.SnapshotData,
        });
    }
    return Result;
}

FRouterUpsertPlayerRouteRequest BuildSceneRouteRequest(uint64 PlayerId, uint32 SceneId)
{
    FRouterUpsertPlayerRouteRequest RouteRequest;
    RouteRequest.PlayerId = PlayerId;
    RouteRequest.TargetServerType = static_cast<uint8>(EServerType::Scene);
    RouteRequest.SceneId = SceneId;
    return RouteRequest;
}

FPlayerUpdateRouteRequest BuildPlayerSceneRouteUpdateRequest(uint64 PlayerId, uint32 SceneId)
{
    FPlayerUpdateRouteRequest UpdateRouteRequest;
    UpdateRouteRequest.PlayerId = PlayerId;
    UpdateRouteRequest.SceneId = SceneId;
    UpdateRouteRequest.TargetServerType = static_cast<uint8>(EServerType::Scene);
    return UpdateRouteRequest;
}

FSceneEnterRequest BuildSceneEnterRequest(uint64 PlayerId, uint32 SceneId)
{
    FSceneEnterRequest SceneRequest;
    SceneRequest.PlayerId = PlayerId;
    SceneRequest.SceneId = SceneId;
    return SceneRequest;
}

FSceneLeaveRequest BuildSceneLeaveRequest(uint64 PlayerId)
{
    FSceneLeaveRequest LeaveRequest;
    LeaveRequest.PlayerId = PlayerId;
    return LeaveRequest;
}
}

class FPlayerEnterWorldWorkflow final
    : public MServerCallAsyncSupport::TServerCallWorkflow<FPlayerEnterWorldWorkflow, FPlayerEnterWorldResponse>
{
public:
    using TResponseType = FPlayerEnterWorldResponse;

    FPlayerEnterWorldWorkflow(MWorldPlayerServiceEndpoint* InService, FPlayerEnterWorldRequest InRequest)
        : Service(InService)
        , Request(std::move(InRequest))
    {
    }

protected:
    void OnStart() override
    {
        FLoginValidateSessionRequest ValidateRequest;
        ValidateRequest.PlayerId = Request.PlayerId;
        ValidateRequest.SessionKey = Request.SessionKey;
        Continue(Service->LoginRpc->ValidateSessionCall(ValidateRequest), &FPlayerEnterWorldWorkflow::OnSessionValidated);
    }

private:
    void OnSessionValidated(const FLoginValidateSessionResponse& ValidateResponse)
    {
        if (!ValidateResponse.bValid)
        {
            Fail("session_invalid", "PlayerEnterWorld");
            return;
        }

        FMgoLoadPlayerRequest LoadRequest;
        LoadRequest.PlayerId = Request.PlayerId;
        Continue(Service->MgoRpc->LoadPlayer(LoadRequest), &FPlayerEnterWorldWorkflow::OnPlayerLoaded);
    }

    void OnPlayerLoaded(const FMgoLoadPlayerResponse& LoadResponse)
    {
        MPlayer* Player = Service->FindOrCreatePlayer(Request.PlayerId);
        if (!Player)
        {
            Fail("player_create_failed", "PlayerEnterWorld");
            return;
        }

        Player->InitializeForLogin(Request.PlayerId, Request.GatewayConnectionId, Request.SessionKey);
        if (!LoadResponse.Records.empty())
        {
            MString ApplyError;
            if (!MObjectDomainUtils::ApplyObjectDomainSnapshotRecords(
                    Player,
                    FilterCompatiblePlayerRecords(LoadResponse.Records),
                    EPropertyDomainFlags::Persistence,
                    &ApplyError))
            {
                Fail("player_state_apply_failed", ApplyError.c_str());
                return;
            }
        }

        Player->FinalizeLoadedState();

        Continue(
            Service->EnterSceneForPlayer(Request.PlayerId, Player->ResolveCurrentSceneId()),
            &FPlayerEnterWorldWorkflow::OnSceneEntered);
    }

    void OnSceneEntered(const FSceneEnterResponse& SceneResponse)
    {
        TargetSceneId = SceneResponse.SceneId;
        Continue(
            Service->ApplySceneRouteForPlayer(Request.PlayerId, TargetSceneId),
            &FPlayerEnterWorldWorkflow::OnRouteApplied);
    }

    void OnRouteApplied(const FPlayerUpdateRouteResponse&)
    {
        if (!Service->FindPlayer(Request.PlayerId))
        {
            Fail("player_missing", "PlayerEnterWorld");
            return;
        }

        Succeed(BuildPlayerOnlyResponse<FPlayerEnterWorldResponse>(Request.PlayerId));
    }

    MWorldPlayerServiceEndpoint* Service = nullptr;
    FPlayerEnterWorldRequest Request;
    uint32 TargetSceneId = 0;
};

class FPlayerLogoutWorkflow final
    : public MServerCallAsyncSupport::TServerCallWorkflow<FPlayerLogoutWorkflow, FPlayerLogoutResponse>
{
public:
    using TResponseType = FPlayerLogoutResponse;

    FPlayerLogoutWorkflow(MWorldPlayerServiceEndpoint* InService, FPlayerLogoutRequest InRequest)
        : Service(InService)
        , Request(std::move(InRequest))
    {
    }

protected:
    void OnStart() override
    {
        Player = Service->FindPlayer(Request.PlayerId);
        if (!Player)
        {
            FPlayerLogoutResponse Response;
            Response.PlayerId = Request.PlayerId;
            Succeed(std::move(Response));
            return;
        }

        if (Player->ResolveCurrentSceneId() != 0 && Service->SceneRpc && Service->SceneRpc->IsAvailable())
        {
            Continue(Service->LeaveSceneForPlayer(Request.PlayerId, Player->ResolveCurrentSceneId()), &FPlayerLogoutWorkflow::OnSceneLeft);
            return;
        }

        OnSceneLeft(FSceneLeaveResponse{Request.PlayerId});
    }

private:
    void OnSceneLeft(const FSceneLeaveResponse&)
    {
        if (Player)
        {
            Player->PrepareForLogout();
            SaveRequest.PlayerId = Request.PlayerId;
            SaveRequest.Records = ToProtocolPersistenceRecords(
                Service->PersistenceSubsystem->BuildRecordsForRoot(Player, false));
        }

        Continue(Service->MgoRpc->SavePlayer(SaveRequest), &FPlayerLogoutWorkflow::OnPlayerSaved);
    }

    void OnPlayerSaved(const FMgoSavePlayerResponse&)
    {
        Service->RemovePlayer(Request.PlayerId);
        Succeed(BuildPlayerOnlyResponse<FPlayerLogoutResponse>(Request.PlayerId));
    }

    MWorldPlayerServiceEndpoint* Service = nullptr;
    FPlayerLogoutRequest Request;
    MPlayer* Player = nullptr;
    FMgoSavePlayerRequest SaveRequest;
};

class FPlayerSwitchSceneWorkflow final
    : public MServerCallAsyncSupport::TServerCallWorkflow<FPlayerSwitchSceneWorkflow, FPlayerSwitchSceneResponse>
{
public:
    using TResponseType = FPlayerSwitchSceneResponse;

    FPlayerSwitchSceneWorkflow(MWorldPlayerServiceEndpoint* InService, FPlayerSwitchSceneRequest InRequest)
        : Service(InService)
        , Request(std::move(InRequest))
    {
    }

protected:
    void OnStart() override
    {
        Player = Service->FindPlayer(Request.PlayerId);
        if (!Player)
        {
            Fail("player_not_found", "PlayerSwitchScene");
            return;
        }

        CurrentSceneId = Player->ResolveCurrentSceneId();
        TargetSceneId = Request.SceneId;

        if (CurrentSceneId == TargetSceneId)
        {
            OnSceneEntered(FSceneEnterResponse{Request.PlayerId, TargetSceneId});
            return;
        }

        if (CurrentSceneId != 0)
        {
            Continue(Service->LeaveSceneForPlayer(Request.PlayerId, CurrentSceneId), &FPlayerSwitchSceneWorkflow::OnSceneLeft);
            return;
        }

        EnterTargetScene();
    }

private:
    void OnSceneLeft(const FSceneLeaveResponse&)
    {
        EnterTargetScene();
    }

    void EnterTargetScene()
    {
        Continue(Service->EnterSceneForPlayer(Request.PlayerId, TargetSceneId), &FPlayerSwitchSceneWorkflow::OnSceneEntered);
    }

    void OnSceneEntered(const FSceneEnterResponse& SceneResponse)
    {
        TargetSceneId = SceneResponse.SceneId;
        Continue(
            Service->ApplySceneRouteForPlayer(Request.PlayerId, TargetSceneId),
            &FPlayerSwitchSceneWorkflow::OnRouteApplied);
    }

    void OnRouteApplied(const FPlayerUpdateRouteResponse&)
    {
        if (!Player)
        {
            Fail("player_missing", "PlayerSwitchScene");
            return;
        }

        FPlayerSwitchSceneResponse Response;
        Response.PlayerId = Request.PlayerId;
        Response.SceneId = TargetSceneId;
        Succeed(std::move(Response));
    }

    MWorldPlayerServiceEndpoint* Service = nullptr;
    FPlayerSwitchSceneRequest Request;
    MPlayer* Player = nullptr;
    uint32 CurrentSceneId = 0;
    uint32 TargetSceneId = 0;
};
}

void MWorldPlayerServiceEndpoint::Initialize(
    TMap<uint64, MPlayer*>* InOnlinePlayers,
    MPersistenceSubsystem* InPersistenceSubsystem,
    MWorldLoginRpc* InLoginRpc,
    MWorldMgoRpc* InMgoRpc,
    MWorldSceneRpc* InSceneRpc,
    MWorldRouterRpc* InRouterRpc)
{
    OnlinePlayers = InOnlinePlayers;
    PersistenceSubsystem = InPersistenceSubsystem;
    LoginRpc = InLoginRpc;
    MgoRpc = InMgoRpc;
    SceneRpc = InSceneRpc;
    RouterRpc = InRouterRpc;
}

MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerEnterWorld(
    const FPlayerEnterWorldRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerEnterWorldResponse>("player_id_required", "PlayerEnterWorld");
    }

    if (auto Error = ValidateDependencies<FPlayerEnterWorldResponse>(
            "PlayerEnterWorld",
            {
                EWorldPlayerServiceDependency::OnlinePlayers,
                EWorldPlayerServiceDependency::Login,
                EWorldPlayerServiceDependency::Mgo,
                EWorldPlayerServiceDependency::Scene,
                EWorldPlayerServiceDependency::Router,
            });
        Error.has_value())
    {
        return std::move(*Error);
    }

    return MServerCallAsyncSupport::StartWorkflow<MWorldPlayerServiceFlows::FPlayerEnterWorldWorkflow>(this, Request);
}

MFuture<TResult<FPlayerFindResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerFind(const FPlayerFindRequest& Request)
{
    return PlayerProxyCall().PlayerFind(Request);
}

#define M_WORLD_PLAYER_PROXY_ROUTE(ServiceMethod, RequestType, ResponseType, NodeName, PlayerFunctionName) \
MFuture<TResult<ResponseType, FAppError>> MWorldPlayerServiceEndpoint::ServiceMethod( \
    const RequestType& Request) \
{ \
    return PlayerProxyCall().ServiceMethod(Request); \
}
#include "Servers/World/Services/WorldPlayerProxyRouteList.inl"
#undef M_WORLD_PLAYER_PROXY_ROUTE

MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> MWorldPlayerServiceEndpoint::ApplySceneRouteForPlayer(
    uint64 PlayerId,
    uint32 SceneId) const
{
    if (auto Error = ValidateDependencies<FPlayerUpdateRouteResponse>(
            "ApplySceneRouteForPlayer",
            {
                EWorldPlayerServiceDependency::OnlinePlayers,
                EWorldPlayerServiceDependency::Router,
            });
        Error.has_value())
    {
        return std::move(*Error);
    }

    const FRouterUpsertPlayerRouteRequest RouteRequest = MWorldPlayerServiceFlows::BuildSceneRouteRequest(PlayerId, SceneId);
    return MServerCallAsyncSupport::Chain(
        RouterRpc->UpsertPlayerRoute(RouteRequest),
        [this, PlayerId, SceneId](const FRouterUpsertPlayerRouteResponse&)
        {
            return DispatchBoundPlayerRequest<FPlayerUpdateRouteResponse>(
                MWorldPlayerServiceFlows::BuildPlayerSceneRouteUpdateRequest(PlayerId, SceneId),
                MPlayerProxyCall::EObjectProxyPlayerNode::Controller,
                "PlayerUpdateRoute",
                "ApplySceneRouteForPlayer");
        });
}

MFuture<TResult<FSceneEnterResponse, FAppError>> MWorldPlayerServiceEndpoint::EnterSceneForPlayer(
    uint64 PlayerId,
    uint32 SceneId) const
{
    if (auto Error = ValidateDependencies<FSceneEnterResponse>(
            "EnterSceneForPlayer",
            {
                EWorldPlayerServiceDependency::Scene,
            });
        Error.has_value())
    {
        return std::move(*Error);
    }

    if (PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneEnterResponse>("player_id_required", "EnterSceneForPlayer");
    }

    if (SceneId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneEnterResponse>("scene_id_required", "EnterSceneForPlayer");
    }

    return SceneRpc->EnterScene(MWorldPlayerServiceFlows::BuildSceneEnterRequest(PlayerId, SceneId));
}

MFuture<TResult<FSceneLeaveResponse, FAppError>> MWorldPlayerServiceEndpoint::LeaveSceneForPlayer(
    uint64 PlayerId,
    uint32 CurrentSceneId) const
{
    if (PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneLeaveResponse>("player_id_required", "LeaveSceneForPlayer");
    }

    if (CurrentSceneId == 0)
    {
        return MServerCallAsyncSupport::MakeSuccessFuture(FSceneLeaveResponse{PlayerId});
    }

    if (auto Error = ValidateDependencies<FSceneLeaveResponse>(
            "LeaveSceneForPlayer",
            {
                EWorldPlayerServiceDependency::Scene,
            });
        Error.has_value())
    {
        return std::move(*Error);
    }

    return SceneRpc->LeaveScene(MWorldPlayerServiceFlows::BuildSceneLeaveRequest(PlayerId));
}

MFuture<TResult<FPlayerLogoutResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerLogout(
    const FPlayerLogoutRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerLogoutResponse>("player_id_required", "PlayerLogout");
    }

    if (auto Error = ValidateDependencies<FPlayerLogoutResponse>(
            "PlayerLogout",
            {
                EWorldPlayerServiceDependency::OnlinePlayers,
                EWorldPlayerServiceDependency::Persistence,
                EWorldPlayerServiceDependency::Mgo,
            });
        Error.has_value())
    {
        return std::move(*Error);
    }

    if (!FindPlayer(Request.PlayerId))
    {
        return MServerCallAsyncSupport::MakeSuccessFuture(
            MWorldPlayerServiceFlows::BuildPlayerOnlyResponse<FPlayerLogoutResponse>(Request.PlayerId));
    }

    return MServerCallAsyncSupport::StartWorkflow<MWorldPlayerServiceFlows::FPlayerLogoutWorkflow>(this, Request);
}

MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerSwitchScene(
    const FPlayerSwitchSceneRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerSwitchSceneResponse>("player_id_required", "PlayerSwitchScene");
    }

    if (Request.SceneId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerSwitchSceneResponse>("scene_id_required", "PlayerSwitchScene");
    }

    if (auto Error = ValidateDependencies<FPlayerSwitchSceneResponse>(
            "PlayerSwitchScene",
            {
                EWorldPlayerServiceDependency::OnlinePlayers,
                EWorldPlayerServiceDependency::Scene,
                EWorldPlayerServiceDependency::Router,
            });
        Error.has_value())
    {
        return std::move(*Error);
    }

    return MServerCallAsyncSupport::StartWorkflow<MWorldPlayerServiceFlows::FPlayerSwitchSceneWorkflow>(this, Request);
}

MPlayer* MWorldPlayerServiceEndpoint::FindPlayer(uint64 PlayerId) const
{
    if (!OnlinePlayers)
    {
        return nullptr;
    }

    auto It = OnlinePlayers->find(PlayerId);
    return (It != OnlinePlayers->end()) ? It->second : nullptr;
}

MPlayer* MWorldPlayerServiceEndpoint::FindOrCreatePlayer(uint64 PlayerId)
{
    if (!OnlinePlayers)
    {
        return nullptr;
    }

    if (MPlayer* Existing = FindPlayer(PlayerId))
    {
        return Existing;
    }

    MObject* Owner = GetOuter() ? GetOuter() : static_cast<MObject*>(this);
    MPlayer* Player = NewMObject<MPlayer>(Owner, "Player_" + MStringUtil::ToString(PlayerId));
    (*OnlinePlayers)[PlayerId] = Player;
    return Player;
}

void MWorldPlayerServiceEndpoint::RemovePlayer(uint64 PlayerId)
{
    if (!OnlinePlayers)
    {
        return;
    }

    auto It = OnlinePlayers->find(PlayerId);
    if (It == OnlinePlayers->end())
    {
        return;
    }

    MPlayer* Player = It->second;
    OnlinePlayers->erase(It);
    DestroyMObject(Player);
}
