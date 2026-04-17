#include "Servers/World/Player/PlayerService.h"

#include "Common/Runtime/Concurrency/FiberAwait.h"
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

MFuture<TResult<FPlayerLogoutResponse, FAppError>> MPlayerService::PlayerLogout(const FPlayerLogoutRequest& Request)
{
    return DispatchRuntimeCommandMany<FPlayerLogoutResponse>(
        Request,
        BuildLogoutParticipants(Request.PlayerId),
        "PlayerLogout",
        {
            EDependency::Persistence,
            EDependency::Mgo,
        },
        &MPlayerService::DoPlayerLogout);
}

TResult<FPlayerLogoutResponse, FAppError> MPlayerService::DoPlayerLogout(FPlayerLogoutRequest Request)
{
    MPlayer* Player = FindPlayer(Request.PlayerId);
    if (!Player)
    {
        return TResult<FPlayerLogoutResponse, FAppError>::Ok(
            BuildPlayerOnlyResponse<FPlayerLogoutResponse>(Request.PlayerId));
    }

    const uint32 SceneIdBeforeLogout = Player->ResolveCurrentSceneId();
    if (SceneIdBeforeLogout != 0 &&
        WorldServer->GetScene() &&
        WorldServer->GetScene()->IsAvailable())
    {
        (void)MAwaitOk(LeaveSceneForPlayer(Request.PlayerId, SceneIdBeforeLogout));
    }

    Player = FindPlayer(Request.PlayerId);
    if (!Player)
    {
        return TResult<FPlayerLogoutResponse, FAppError>::Ok(
            BuildPlayerOnlyResponse<FPlayerLogoutResponse>(Request.PlayerId));
    }

    CleanupPlayerSocialState(Request.PlayerId);
    Player->PrepareForLogout();

    FMgoSavePlayerRequest SaveRequest;
    SaveRequest.PlayerId = Request.PlayerId;
    SaveRequest.Records = ToProtocolPersistenceRecords(
        WorldServer->GetPersistence().BuildRecordsForRoot(Player, false));
    (void)MAwaitOk(WorldServer->GetMgo()->SavePlayer(SaveRequest));

    PlayerCommandRuntime->BumpEpoch(Request.PlayerId);
    RemovePlayer(Request.PlayerId);

    if (SceneIdBeforeLogout != 0)
    {
        QueueScenePlayerLeaveNotify(Request.PlayerId, SceneIdBeforeLogout);
    }

    return TResult<FPlayerLogoutResponse, FAppError>::Ok(
        BuildPlayerOnlyResponse<FPlayerLogoutResponse>(Request.PlayerId));
}
