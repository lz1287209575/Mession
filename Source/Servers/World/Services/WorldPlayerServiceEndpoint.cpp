#include "Servers/World/Services/WorldPlayerServiceEndpoint.h"

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

TVector<SObjectDomainSnapshotRecord> ToRuntimePersistenceRecords(const TVector<FObjectPersistenceRecord>& Records)
{
    TVector<SObjectDomainSnapshotRecord> Result;
    Result.reserve(Records.size());
    for (const FObjectPersistenceRecord& Record : Records)
    {
        Result.push_back(SObjectDomainSnapshotRecord{
            0,
            0,
            0,
            Record.ObjectPath,
            Record.ClassName,
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
        MPlayerSession* Session = Service->FindOrCreatePlayerSession(Request.PlayerId);
        if (!Session)
        {
            Fail("player_session_create_failed", "PlayerEnterWorld");
            return;
        }

        Session->InitializeForLogin(Request.PlayerId, Request.GatewayConnectionId, Request.SessionKey);
        if (!LoadResponse.Records.empty())
        {
            MString ApplyError;
            if (!MObjectDomainUtils::ApplyObjectDomainSnapshotRecords(
                    Session,
                    ToRuntimePersistenceRecords(LoadResponse.Records),
                    EPropertyDomainFlags::Persistence,
                    &ApplyError))
            {
                Fail("player_state_apply_failed", ApplyError.c_str());
                return;
            }
        }

        FSceneEnterRequest SceneRequest;
        SceneRequest.PlayerId = Request.PlayerId;
        SceneRequest.SceneId = Session->SceneId;
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
        MPlayerSession* Session = Service->FindPlayerSession(Request.PlayerId);
        if (!Session)
        {
            Fail("player_session_missing", "PlayerEnterWorld");
            return;
        }

        Session->SetRoute(TargetSceneId, static_cast<uint8>(EServerType::Scene));

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
        MPlayerSession* Session = Service->FindPlayerSession(Request.PlayerId);
        if (!Session)
        {
            FPlayerLogoutResponse Response;
            Response.PlayerId = Request.PlayerId;
            Succeed(std::move(Response));
            return;
        }

        FMgoSavePlayerRequest SaveRequest;
        SaveRequest.PlayerId = Request.PlayerId;
        if (!Service->PersistenceSubsystem)
        {
            Fail("persistence_subsystem_missing", "PlayerLogout");
            return;
        }

        SaveRequest.Records = ToProtocolPersistenceRecords(
            Service->PersistenceSubsystem->BuildRecordsForRoot(Session, false));
        Continue(Service->MgoRpc->SavePlayer(SaveRequest), &FPlayerLogoutWorkflow::OnPlayerSaved);
    }

private:
    void OnPlayerSaved(const FMgoSavePlayerResponse&)
    {
        Service->RemovePlayerSession(Request.PlayerId);

        FPlayerLogoutResponse Response;
        Response.PlayerId = Request.PlayerId;
        Succeed(std::move(Response));
    }

    MWorldPlayerServiceEndpoint* Service = nullptr;
    FPlayerLogoutRequest Request;
};
}

void MWorldPlayerServiceEndpoint::Initialize(
    TMap<uint64, MPlayerSession*>* InOnlinePlayers,
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

    FPlayerFindResponse Response;
    Response.PlayerId = Request.PlayerId;
    if (MPlayerSession* Session = FindPlayerSession(Request.PlayerId))
    {
        Response.bFound = true;
        Response.GatewayConnectionId = Session->GatewayConnectionId;
        Response.SceneId = Session->SceneId;
    }

    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> MWorldPlayerServiceEndpoint::PlayerUpdateRoute(
    const FPlayerUpdateRouteRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerUpdateRouteResponse>("player_id_required", "PlayerUpdateRoute");
    }

    if (!OnlinePlayers)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerUpdateRouteResponse>("world_service_not_initialized", "PlayerUpdateRoute");
    }

    MPlayerSession* Session = FindPlayerSession(Request.PlayerId);
    if (!Session)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerUpdateRouteResponse>("player_not_found", "PlayerUpdateRoute");
    }

    Session->SetRoute(Request.SceneId, Request.TargetServerType);

    FPlayerUpdateRouteResponse Response;
    Response.PlayerId = Request.PlayerId;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
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

    if (!FindPlayerSession(Request.PlayerId))
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

    if (!OnlinePlayers)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerSwitchSceneResponse>("world_service_not_initialized", "PlayerSwitchScene");
    }

    MPlayerSession* Session = FindPlayerSession(Request.PlayerId);
    if (!Session)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerSwitchSceneResponse>("player_not_found", "PlayerSwitchScene");
    }

    Session->SetRoute(Request.SceneId, static_cast<uint8>(EServerType::Scene));

    FPlayerSwitchSceneResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.SceneId = Request.SceneId;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MPlayerSession* MWorldPlayerServiceEndpoint::FindPlayerSession(uint64 PlayerId) const
{
    if (!OnlinePlayers)
    {
        return nullptr;
    }

    auto It = OnlinePlayers->find(PlayerId);
    return (It != OnlinePlayers->end()) ? It->second : nullptr;
}

MPlayerSession* MWorldPlayerServiceEndpoint::FindOrCreatePlayerSession(uint64 PlayerId)
{
    if (!OnlinePlayers)
    {
        return nullptr;
    }

    if (MPlayerSession* Existing = FindPlayerSession(PlayerId))
    {
        return Existing;
    }

    MObject* Owner = GetOuter() ? GetOuter() : static_cast<MObject*>(this);
    MPlayerSession* Session = NewMObject<MPlayerSession>(Owner, "PlayerSession_" + MStringUtil::ToString(PlayerId));
    (*OnlinePlayers)[PlayerId] = Session;
    return Session;
}

void MWorldPlayerServiceEndpoint::RemovePlayerSession(uint64 PlayerId)
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

    MPlayerSession* Session = It->second;
    OnlinePlayers->erase(It);
    DestroyMObject(Session);
}
