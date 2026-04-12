#include "Servers/Mgo/MgoPlayerState.h"

void MMgoPlayerState::Initialize(TMap<uint64, TVector<FObjectPersistenceRecord>>* InPlayerRecords)
{
    PlayerRecords = InPlayerRecords;
}

MFuture<TResult<FMgoLoadPlayerResponse, FAppError>> MMgoPlayerState::LoadPlayer(
    const FMgoLoadPlayerRequest& Request)
{
    if (!PlayerRecords)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FMgoLoadPlayerResponse>("mgo_service_not_initialized", "LoadPlayer");
    }

    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FMgoLoadPlayerResponse>("player_id_required", "LoadPlayer");
    }

    FMgoLoadPlayerResponse Response;
    Response.PlayerId = Request.PlayerId;
    auto It = PlayerRecords->find(Request.PlayerId);
    if (It != PlayerRecords->end())
    {
        Response.Records = It->second;
    }

    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FMgoSavePlayerResponse, FAppError>> MMgoPlayerState::SavePlayer(
    const FMgoSavePlayerRequest& Request)
{
    if (!PlayerRecords)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FMgoSavePlayerResponse>("mgo_service_not_initialized", "SavePlayer");
    }

    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FMgoSavePlayerResponse>("player_id_required", "SavePlayer");
    }

    (*PlayerRecords)[Request.PlayerId] = Request.Records;

    FMgoSavePlayerResponse Response;
    Response.PlayerId = Request.PlayerId;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

