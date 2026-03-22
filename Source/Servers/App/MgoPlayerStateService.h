#pragma once

#include "Common/Runtime/MLib.h"
#include "Protocol/Messages/BackendServiceMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

class MMgoPlayerStateService
{
public:
    MFuture<TResult<FMgoLoadPlayerResponse, FAppError>> LoadPlayer(
        const TMap<uint64, MString>& PlayerPayloads,
        const FMgoLoadPlayerRequest& Request) const
    {
        if (Request.PlayerId == 0)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FMgoLoadPlayerResponse>("player_id_required", "LoadPlayer");
        }

        FMgoLoadPlayerResponse Response;
        Response.PlayerId = Request.PlayerId;
        auto It = PlayerPayloads.find(Request.PlayerId);
        if (It != PlayerPayloads.end())
        {
            Response.Payload = It->second;
        }
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    MFuture<TResult<FMgoSavePlayerResponse, FAppError>> SavePlayer(
        TMap<uint64, MString>& PlayerPayloads,
        const FMgoSavePlayerRequest& Request) const
    {
        if (Request.PlayerId == 0)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FMgoSavePlayerResponse>("player_id_required", "SavePlayer");
        }

        PlayerPayloads[Request.PlayerId] = Request.Payload;

        FMgoSavePlayerResponse Response;
        Response.PlayerId = Request.PlayerId;
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }
};
