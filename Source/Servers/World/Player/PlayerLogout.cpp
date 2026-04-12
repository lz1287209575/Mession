#include "Servers/World/Player/PlayerService.h"

#include "Common/Runtime/Persistence/PersistenceSubsystem.h"
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
}

namespace MPlayerActions
{
class FPlayerLogoutAction final
    : public MServerCallAsyncSupport::TServerCallAction<FPlayerLogoutAction, FPlayerLogoutResponse>
{
public:
    using TResponseType = FPlayerLogoutResponse;

    FPlayerLogoutAction(MPlayerService* InPlayerService, FPlayerLogoutRequest InRequest)
        : PlayerService(InPlayerService)
        , Request(std::move(InRequest))
    {
    }

protected:
    void OnStart() override
    {
        Player = PlayerService->FindPlayer(Request.PlayerId);
        if (!Player)
        {
            FPlayerLogoutResponse Response;
            Response.PlayerId = Request.PlayerId;
            Succeed(std::move(Response));
            return;
        }

        if (Player->ResolveCurrentSceneId() != 0 &&
            PlayerService->WorldServer->GetScene() &&
            PlayerService->WorldServer->GetScene()->IsAvailable())
        {
            Continue(PlayerService->LeaveSceneForPlayer(Request.PlayerId, Player->ResolveCurrentSceneId()), &FPlayerLogoutAction::OnSceneLeft);
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
                PlayerService->WorldServer->GetPersistence().BuildRecordsForRoot(Player, false));
        }

        Continue(PlayerService->WorldServer->GetMgo()->SavePlayer(SaveRequest), &FPlayerLogoutAction::OnPlayerSaved);
    }

    void OnPlayerSaved(const FMgoSavePlayerResponse&)
    {
        PlayerService->RemovePlayer(Request.PlayerId);
        Succeed(BuildPlayerOnlyResponse<FPlayerLogoutResponse>(Request.PlayerId));
    }

    MPlayerService* PlayerService = nullptr;
    FPlayerLogoutRequest Request;
    MPlayer* Player = nullptr;
    FMgoSavePlayerRequest SaveRequest;
};
}

MFuture<TResult<FPlayerLogoutResponse, FAppError>> MPlayerService::PlayerLogout(const FPlayerLogoutRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerLogoutResponse>("player_id_required", "PlayerLogout");
    }

    if (auto Error = ValidateDependencies<FPlayerLogoutResponse>(
            "PlayerLogout",
            {
                EDependency::Persistence,
                EDependency::Mgo,
            });
        Error.has_value())
    {
        return std::move(*Error);
    }

    if (!FindPlayer(Request.PlayerId))
    {
        return MServerCallAsyncSupport::MakeSuccessFuture(BuildPlayerOnlyResponse<FPlayerLogoutResponse>(Request.PlayerId));
    }

    uint32 SceneIdBeforeLogout = 0;
    if (const MPlayer* Player = FindPlayer(Request.PlayerId))
    {
        SceneIdBeforeLogout = Player->ResolveCurrentSceneId();
    }

    MFuture<TResult<FPlayerLogoutResponse, FAppError>> Future =
        MServerCallAsyncSupport::StartAction<MPlayerActions::FPlayerLogoutAction>(this, Request);
    Future.Then([this, PlayerId = Request.PlayerId, SceneIdBeforeLogout](MFuture<TResult<FPlayerLogoutResponse, FAppError>> Completed)
    {
        try
        {
            const TResult<FPlayerLogoutResponse, FAppError> Result = Completed.Get();
            if (Result.IsOk() && SceneIdBeforeLogout != 0)
            {
                QueueScenePlayerLeaveNotify(PlayerId, SceneIdBeforeLogout);
            }
        }
        catch (...)
        {
        }
    });
    return Future;
}

