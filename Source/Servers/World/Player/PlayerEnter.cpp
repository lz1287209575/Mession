#include "Servers/World/Player/PlayerService.h"

#include "Common/Runtime/Concurrency/FiberAwait.h"
#include "Common/Runtime/Persistence/PersistenceSubsystem.h"
#include "Common/Runtime/StringUtils.h"
#include "Protocol/Messages/Auth/AuthSessionMessages.h"
#include "Protocol/Messages/Mgo/MgoPlayerStateMessages.h"
#include "Servers/World/Player/Player.h"

namespace
{
template<typename TResponse>
TResponse BuildPlayerOnlyResponse(uint64 PlayerId)
{
    TResponse Response;
    Response.PlayerId = PlayerId;
    return Response;
}

template<typename TResponse>
TResult<TResponse, FAppError> MakePlayerServiceError(const char* Code, const char* Message = "")
{
    return MakeErrorResult<TResponse>(FAppError::Make(
        Code ? Code : "player_command_failed",
        Message ? Message : ""));
}

TVector<SObjectDomainSnapshotRecord> FilterCompatiblePlayerRecords(const TVector<FObjectPersistenceRecord>& Records)
{
    TVector<SObjectDomainSnapshotRecord> Result;
    Result.reserve(Records.size());
    for (const FObjectPersistenceRecord& Record : Records)
    {
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

MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> MPlayerService::PlayerEnterWorld(
    const FPlayerEnterWorldRequest& Request)
{
    return DispatchRuntimeCommand<FPlayerEnterWorldResponse>(
        Request,
        "PlayerEnterWorld",
        {
            EDependency::Login,
            EDependency::Mgo,
            EDependency::Scene,
            EDependency::Router,
        },
        &MPlayerService::DoPlayerEnterWorld);
}

TResult<FPlayerEnterWorldResponse, FAppError> MPlayerService::DoPlayerEnterWorld(FPlayerEnterWorldRequest Request)
{
    FLoginValidateSessionRequest ValidateRequest;
    ValidateRequest.PlayerId = Request.PlayerId;
    ValidateRequest.SessionKey = Request.SessionKey;

    const FLoginValidateSessionResponse ValidateResponse =
        MAwaitOk(WorldServer->GetLogin()->ValidateSession(ValidateRequest));
    if (!ValidateResponse.bValid)
    {
        return MakePlayerServiceError<FPlayerEnterWorldResponse>("session_invalid", "PlayerEnterWorld");
    }

    FMgoLoadPlayerRequest LoadRequest;
    LoadRequest.PlayerId = Request.PlayerId;
    const FMgoLoadPlayerResponse LoadResponse =
        MAwaitOk(WorldServer->GetMgo()->LoadPlayer(LoadRequest));

    MPlayer* Player = FindOrCreatePlayer(Request.PlayerId);
    if (!Player)
    {
        return MakePlayerServiceError<FPlayerEnterWorldResponse>("player_create_failed", "PlayerEnterWorld");
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
            return MakePlayerServiceError<FPlayerEnterWorldResponse>(
                "player_state_apply_failed",
                ApplyError.c_str());
        }
    }

    Player->FinalizeLoadedState();

    const FSceneEnterResponse SceneResponse =
        MAwaitOk(EnterSceneForPlayer(Request.PlayerId, Player->ResolveCurrentSceneId()));
    if (const TResult<FPlayerUpdateRouteResponse, FAppError> RouteResult =
            ApplySceneRouteForPlayer(Request.PlayerId, SceneResponse.SceneId);
        !RouteResult.IsOk())
    {
        return MakeErrorResult<FPlayerEnterWorldResponse>(RouteResult.GetError());
    }

    Player = FindPlayer(Request.PlayerId);
    if (!Player)
    {
        return MakePlayerServiceError<FPlayerEnterWorldResponse>("player_missing", "PlayerEnterWorld");
    }

    Player->SyncRuntimeStateToProfile();

    QueueScenePlayerEnterNotify(Request.PlayerId);
    return TResult<FPlayerEnterWorldResponse, FAppError>::Ok(
        BuildPlayerOnlyResponse<FPlayerEnterWorldResponse>(Request.PlayerId));
}
