#include "Servers/World/Services/WorldPlayerServiceEndpoint.h"

void MWorldPlayerServiceEndpoint::Initialize(
    TMap<uint64, MPlayerSession*>* InOnlinePlayers,
    MWorldLoginRpc* InLoginRpc,
    MWorldMgoRpc* InMgoRpc,
    MWorldSceneRpc* InSceneRpc,
    MWorldRouterRpc* InRouterRpc)
{
    OnlinePlayers = InOnlinePlayers;
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

    MPromise<TResult<FPlayerEnterWorldResponse, FAppError>> Promise;
    MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> Future = Promise.GetFuture();

    FLoginValidateSessionRequest ValidateRequest;
    ValidateRequest.PlayerId = Request.PlayerId;
    ValidateRequest.SessionKey = Request.SessionKey;

    LoginRpc->ValidateSessionCall(ValidateRequest)
        .Then(
            [this, Promise, Request](MFuture<TResult<FLoginValidateSessionResponse, FAppError>> ValidateFuture) mutable
            {
                const TResult<FLoginValidateSessionResponse, FAppError> ValidateResult = ValidateFuture.Get();
                if (!ValidateResult.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<FPlayerEnterWorldResponse>(ValidateResult.GetError()));
                    return;
                }

                if (!ValidateResult.GetValue().bValid)
                {
                    Promise.SetValue(MakeErrorResult<FPlayerEnterWorldResponse>(FAppError::Make("session_invalid", "PlayerEnterWorld")));
                    return;
                }

                FMgoLoadPlayerRequest LoadRequest;
                LoadRequest.PlayerId = Request.PlayerId;

                MgoRpc->LoadPlayer(LoadRequest)
                    .Then(
                        [this, Promise, Request](MFuture<TResult<FMgoLoadPlayerResponse, FAppError>> LoadFuture) mutable
                        {
                            const TResult<FMgoLoadPlayerResponse, FAppError> LoadResult = LoadFuture.Get();
                            if (!LoadResult.IsOk())
                            {
                                Promise.SetValue(MakeErrorResult<FPlayerEnterWorldResponse>(LoadResult.GetError()));
                                return;
                            }

                            MPlayerSession* Session = FindOrCreatePlayerSession(Request.PlayerId);
                            if (!Session)
                            {
                                Promise.SetValue(MakeErrorResult<FPlayerEnterWorldResponse>(
                                    FAppError::Make("player_session_create_failed", "PlayerEnterWorld")));
                                return;
                            }

                            Session->InitializeForLogin(Request.PlayerId, Request.GatewayConnectionId, Request.SessionKey);

                            const FMgoLoadPlayerResponse& LoadValue = LoadResult.GetValue();
                            if (!LoadValue.Records.empty())
                            {
                                Session->ApplyPersistenceRecords(LoadValue.Records);
                            }

                            const uint32 TargetSceneId = Session->SceneId;

                            FSceneEnterRequest SceneRequest;
                            SceneRequest.PlayerId = Request.PlayerId;
                            SceneRequest.SceneId = TargetSceneId;

                            SceneRpc->EnterScene(SceneRequest)
                                .Then(
                                    [this, Promise, Request](MFuture<TResult<FSceneEnterResponse, FAppError>> SceneFuture) mutable
                                    {
                                        const TResult<FSceneEnterResponse, FAppError> SceneResult = SceneFuture.Get();
                                        if (!SceneResult.IsOk())
                                        {
                                            Promise.SetValue(MakeErrorResult<FPlayerEnterWorldResponse>(SceneResult.GetError()));
                                            return;
                                        }

                                        const uint32 SceneId = SceneResult.GetValue().SceneId;

                                        FRouterUpsertPlayerRouteRequest RouteRequest;
                                        RouteRequest.PlayerId = Request.PlayerId;
                                        RouteRequest.TargetServerType = static_cast<uint8>(EServerType::Scene);
                                        RouteRequest.SceneId = SceneId;

                                        RouterRpc->UpsertPlayerRoute(RouteRequest)
                                            .Then(
                                                [this, Promise, Request, SceneId](MFuture<TResult<FRouterUpsertPlayerRouteResponse, FAppError>> RouteFuture) mutable
                                                {
                                                    const TResult<FRouterUpsertPlayerRouteResponse, FAppError> RouteResult = RouteFuture.Get();
                                                    if (!RouteResult.IsOk())
                                                    {
                                                        Promise.SetValue(MakeErrorResult<FPlayerEnterWorldResponse>(RouteResult.GetError()));
                                                        return;
                                                    }

                                                    MPlayerSession* Session = FindPlayerSession(Request.PlayerId);
                                                    if (!Session)
                                                    {
                                                        Promise.SetValue(MakeErrorResult<FPlayerEnterWorldResponse>(
                                                            FAppError::Make("player_session_missing", "PlayerEnterWorld")));
                                                        return;
                                                    }

                                                    Session->SetRoute(SceneId, static_cast<uint8>(EServerType::Scene));

                                                    FPlayerEnterWorldResponse Response;
                                                    Response.PlayerId = Request.PlayerId;
                                                    Promise.SetValue(TResult<FPlayerEnterWorldResponse, FAppError>::Ok(std::move(Response)));
                                                });
                                    });
                        });
            });

    return Future;
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

    MPlayerSession* Session = FindPlayerSession(Request.PlayerId);
    if (!Session)
    {
        FPlayerLogoutResponse Response;
        Response.PlayerId = Request.PlayerId;
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    if (!MgoRpc || !MgoRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerLogoutResponse>("mgo_server_unavailable", "PlayerLogout");
    }

    MPromise<TResult<FPlayerLogoutResponse, FAppError>> Promise;
    MFuture<TResult<FPlayerLogoutResponse, FAppError>> Future = Promise.GetFuture();

    FMgoSavePlayerRequest SaveRequest;
    SaveRequest.PlayerId = Request.PlayerId;
    SaveRequest.Records = Session->BuildPersistenceRecords();

    MgoRpc->SavePlayer(SaveRequest)
        .Then(
            [this, Promise, Request](MFuture<TResult<FMgoSavePlayerResponse, FAppError>> SaveFuture) mutable
            {
                const TResult<FMgoSavePlayerResponse, FAppError> SaveResult = SaveFuture.Get();
                if (!SaveResult.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<FPlayerLogoutResponse>(SaveResult.GetError()));
                    return;
                }

                RemovePlayerSession(Request.PlayerId);

                FPlayerLogoutResponse Response;
                Response.PlayerId = Request.PlayerId;
                Promise.SetValue(TResult<FPlayerLogoutResponse, FAppError>::Ok(std::move(Response)));
            });

    return Future;
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
