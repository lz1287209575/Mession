#include "Servers/World/Services/WorldPlayerServiceEndpoint.h"
#include "Common/Runtime/StringUtils.h"

namespace MWorldPlayerServiceFlows
{
namespace
{
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

        FSceneEnterRequest SceneRequest;
        SceneRequest.PlayerId = Request.PlayerId;
        SceneRequest.SceneId = Player->ResolveCurrentSceneId();
        Continue(Service->SceneRpc->EnterScene(SceneRequest), &FPlayerEnterWorldWorkflow::OnSceneEntered);
    }

    void OnSceneEntered(const FSceneEnterResponse& SceneResponse)
    {
        TargetSceneId = SceneResponse.SceneId;

        FRouterUpsertPlayerRouteRequest RouteRequest;
        RouteRequest.PlayerId = Request.PlayerId;
        RouteRequest.TargetServerType = static_cast<uint8>(EServerType::Scene);
        RouteRequest.SceneId = TargetSceneId;
        Continue(Service->RouterRpc->UpsertPlayerRoute(RouteRequest), &FPlayerEnterWorldWorkflow::OnRouteUpdated);
    }

    void OnRouteUpdated(const FRouterUpsertPlayerRouteResponse&)
    {
        FPlayerUpdateRouteRequest UpdateRouteRequest;
        UpdateRouteRequest.PlayerId = Request.PlayerId;
        UpdateRouteRequest.SceneId = TargetSceneId;
        UpdateRouteRequest.TargetServerType = static_cast<uint8>(EServerType::Scene);
        Continue(
            Service->DispatchPlayerRequest(UpdateRouteRequest),
            &FPlayerEnterWorldWorkflow::OnRouteApplied);
    }

    void OnRouteApplied(const FPlayerUpdateRouteResponse&)
    {
        if (!Service->FindPlayer(Request.PlayerId))
        {
            Fail("player_missing", "PlayerEnterWorld");
            return;
        }

        FPlayerEnterWorldResponse Response;
        Response.PlayerId = Request.PlayerId;
        Succeed(std::move(Response));
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

        if (!Service->PersistenceSubsystem)
        {
            Fail("persistence_subsystem_missing", "PlayerLogout");
            return;
        }

        if (Player->ResolveCurrentSceneId() != 0 && Service->SceneRpc && Service->SceneRpc->IsAvailable())
        {
            FSceneLeaveRequest LeaveRequest;
            LeaveRequest.PlayerId = Request.PlayerId;
            Continue(Service->SceneRpc->LeaveScene(LeaveRequest), &FPlayerLogoutWorkflow::OnSceneLeft);
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

        FPlayerLogoutResponse Response;
        Response.PlayerId = Request.PlayerId;
        Succeed(std::move(Response));
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
            FSceneLeaveRequest LeaveRequest;
            LeaveRequest.PlayerId = Request.PlayerId;
            Continue(Service->SceneRpc->LeaveScene(LeaveRequest), &FPlayerSwitchSceneWorkflow::OnSceneLeft);
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
        FSceneEnterRequest SceneRequest;
        SceneRequest.PlayerId = Request.PlayerId;
        SceneRequest.SceneId = TargetSceneId;
        Continue(Service->SceneRpc->EnterScene(SceneRequest), &FPlayerSwitchSceneWorkflow::OnSceneEntered);
    }

    void OnSceneEntered(const FSceneEnterResponse& SceneResponse)
    {
        TargetSceneId = SceneResponse.SceneId;

        FRouterUpsertPlayerRouteRequest RouteRequest;
        RouteRequest.PlayerId = Request.PlayerId;
        RouteRequest.TargetServerType = static_cast<uint8>(EServerType::Scene);
        RouteRequest.SceneId = TargetSceneId;
        Continue(Service->RouterRpc->UpsertPlayerRoute(RouteRequest), &FPlayerSwitchSceneWorkflow::OnRouteUpdated);
    }

    void OnRouteUpdated(const FRouterUpsertPlayerRouteResponse&)
    {
        FPlayerUpdateRouteRequest UpdateRouteRequest;
        UpdateRouteRequest.PlayerId = Request.PlayerId;
        UpdateRouteRequest.SceneId = TargetSceneId;
        UpdateRouteRequest.TargetServerType = static_cast<uint8>(EServerType::Scene);
        Continue(
            Service->DispatchPlayerRequest(UpdateRouteRequest),
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

    if (!OnlinePlayers)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerEnterWorldResponse>("world_service_not_initialized", "PlayerEnterWorld");
    }

    if (!LoginRpc || !LoginRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerEnterWorldResponse>("login_server_unavailable", "PlayerEnterWorld");
    }

    if (!MgoRpc || !MgoRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerEnterWorldResponse>("mgo_server_unavailable", "PlayerEnterWorld");
    }

    if (!SceneRpc || !SceneRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerEnterWorldResponse>("scene_server_unavailable", "PlayerEnterWorld");
    }

    if (!RouterRpc || !RouterRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerEnterWorldResponse>("router_server_unavailable", "PlayerEnterWorld");
    }

    return MServerCallAsyncSupport::StartWorkflow<MWorldPlayerServiceFlows::FPlayerEnterWorldWorkflow>(this, Request);
}

MFuture<TResult<FPlayerFindResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerFind(const FPlayerFindRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerFindResponse>("player_id_required", "PlayerFind");
    }

    if (!OnlinePlayers)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerFindResponse>("world_service_not_initialized", "PlayerFind");
    }

    if (!FindPlayer(Request.PlayerId))
    {
        FPlayerFindResponse Response;
        Response.PlayerId = Request.PlayerId;
        Response.bFound = false;
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    return DispatchPlayerRequest(Request);
}

MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerUpdateRoute(
    const FPlayerUpdateRouteRequest& Request)
{
    return DispatchPlayerRequest(Request);
}

MFuture<TResult<FPlayerQueryProfileResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerQueryProfile(
    const FPlayerQueryProfileRequest& Request)
{
    return DispatchExistingPlayerRequest(Request, "PlayerQueryProfile");
}

MFuture<TResult<FPlayerQueryInventoryResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerQueryInventory(
    const FPlayerQueryInventoryRequest& Request)
{
    return DispatchExistingPlayerRequest(Request, "PlayerQueryInventory");
}

MFuture<TResult<FPlayerQueryProgressionResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerQueryProgression(
    const FPlayerQueryProgressionRequest& Request)
{
    return DispatchExistingPlayerRequest(Request, "PlayerQueryProgression");
}

MFuture<TResult<FPlayerChangeGoldResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerChangeGold(
    const FPlayerChangeGoldRequest& Request)
{
    return DispatchExistingPlayerRequest(Request, "PlayerChangeGold");
}

MFuture<TResult<FPlayerEquipItemResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerEquipItem(
    const FPlayerEquipItemRequest& Request)
{
    return DispatchExistingPlayerRequest(Request, "PlayerEquipItem");
}

MFuture<TResult<FPlayerGrantExperienceResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerGrantExperience(
    const FPlayerGrantExperienceRequest& Request)
{
    return DispatchExistingPlayerRequest(Request, "PlayerGrantExperience");
}

MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerModifyHealth(
    const FPlayerModifyHealthRequest& Request)
{
    return DispatchExistingPlayerRequest(Request, "PlayerModifyHealth");
}

MFuture<TResult<FPlayerLogoutResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerLogout(
    const FPlayerLogoutRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerLogoutResponse>("player_id_required", "PlayerLogout");
    }

    if (!OnlinePlayers)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerLogoutResponse>("world_service_not_initialized", "PlayerLogout");
    }

    if (!FindPlayer(Request.PlayerId))
    {
        FPlayerLogoutResponse Response;
        Response.PlayerId = Request.PlayerId;
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    if (!MgoRpc || !MgoRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerLogoutResponse>("mgo_server_unavailable", "PlayerLogout");
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

    if (!OnlinePlayers)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerSwitchSceneResponse>("world_service_not_initialized", "PlayerSwitchScene");
    }

    if (!SceneRpc || !SceneRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerSwitchSceneResponse>("scene_server_unavailable", "PlayerSwitchScene");
    }

    if (!RouterRpc || !RouterRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerSwitchSceneResponse>("router_server_unavailable", "PlayerSwitchScene");
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
