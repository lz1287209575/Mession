#pragma once

#include "Common/Runtime/MLib.h"
#include "Protocol/Messages/WorldPlayerMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

struct SWorldPlayerState;

class MWorldPlayerService
{
public:
    MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> EnterWorld(
        TMap<uint64, SWorldPlayerState>& OnlinePlayers,
        const FPlayerEnterWorldRequest& Request) const;

    MFuture<TResult<FPlayerFindResponse, FAppError>> FindPlayer(
        const TMap<uint64, SWorldPlayerState>& OnlinePlayers,
        const FPlayerFindRequest& Request) const;

    MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> UpdateRoute(
        TMap<uint64, SWorldPlayerState>& OnlinePlayers,
        const FPlayerUpdateRouteRequest& Request) const;

    MFuture<TResult<FPlayerLogoutResponse, FAppError>> Logout(
        TMap<uint64, SWorldPlayerState>& OnlinePlayers,
        const FPlayerLogoutRequest& Request) const;

    MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> SwitchScene(
        TMap<uint64, SWorldPlayerState>& OnlinePlayers,
        const FPlayerSwitchSceneRequest& Request) const;
};
