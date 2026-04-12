#include "Servers/World/Player/PlayerService.h"

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

namespace MPlayerActions
{
class FPlayerEnterAction final
    : public MServerCallAsyncSupport::TServerCallAction<FPlayerEnterAction, FPlayerEnterWorldResponse>
{
public:
    using TResponseType = FPlayerEnterWorldResponse;

    FPlayerEnterAction(MPlayerService* InPlayerService, FPlayerEnterWorldRequest InRequest)
        : PlayerService(InPlayerService)
        , Request(std::move(InRequest))
    {
    }

protected:
    void OnStart() override
    {
        FLoginValidateSessionRequest ValidateRequest;
        ValidateRequest.PlayerId = Request.PlayerId;
        ValidateRequest.SessionKey = Request.SessionKey;
        Continue(PlayerService->WorldServer->GetLogin()->ValidateSession(ValidateRequest), &FPlayerEnterAction::OnSessionValidated);
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
        Continue(PlayerService->WorldServer->GetMgo()->LoadPlayer(LoadRequest), &FPlayerEnterAction::OnPlayerLoaded);
    }

    void OnPlayerLoaded(const FMgoLoadPlayerResponse& LoadResponse)
    {
        MPlayer* Player = PlayerService->FindOrCreatePlayer(Request.PlayerId);
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
            PlayerService->EnterSceneForPlayer(Request.PlayerId, Player->ResolveCurrentSceneId()),
            &FPlayerEnterAction::OnSceneEntered);
    }

    void OnSceneEntered(const FSceneEnterResponse& SceneResponse)
    {
        TargetSceneId = SceneResponse.SceneId;
        Continue(
            PlayerService->ApplySceneRouteForPlayer(Request.PlayerId, TargetSceneId),
            &FPlayerEnterAction::OnRouteApplied);
    }

    void OnRouteApplied(const FPlayerUpdateRouteResponse&)
    {
        if (!PlayerService->FindPlayer(Request.PlayerId))
        {
            Fail("player_missing", "PlayerEnterWorld");
            return;
        }

        Succeed(BuildPlayerOnlyResponse<FPlayerEnterWorldResponse>(Request.PlayerId));
    }

    MPlayerService* PlayerService = nullptr;
    FPlayerEnterWorldRequest Request;
    uint32 TargetSceneId = 0;
};
}

MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> MPlayerService::PlayerEnterWorld(
    const FPlayerEnterWorldRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerEnterWorldResponse>("player_id_required", "PlayerEnterWorld");
    }

    if (auto Error = ValidateDependencies<FPlayerEnterWorldResponse>(
            "PlayerEnterWorld",
            {
                EDependency::Login,
                EDependency::Mgo,
                EDependency::Scene,
                EDependency::Router,
            });
        Error.has_value())
    {
        return std::move(*Error);
    }

    MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> Future =
        MServerCallAsyncSupport::StartAction<MPlayerActions::FPlayerEnterAction>(this, Request);
    Future.Then([this, PlayerId = Request.PlayerId](MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> Completed)
    {
        try
        {
            const TResult<FPlayerEnterWorldResponse, FAppError> Result = Completed.Get();
            if (Result.IsOk())
            {
                QueueScenePlayerEnterNotify(PlayerId);
            }
        }
        catch (...)
        {
        }
    });
    return Future;
}


