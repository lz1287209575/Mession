#include "Servers/World/Player/PlayerPawn.h"

#include "Servers/World/Player/Player.h"

namespace
{
template<typename TResponse>
TResponse BuildPawnStateResponse(uint64 PlayerId, const MPlayerPawn& Pawn)
{
    TResponse Response;
    Response.PlayerId = PlayerId;
    Response.SceneId = Pawn.SceneId;
    Response.X = Pawn.X;
    Response.Y = Pawn.Y;
    Response.Z = Pawn.Z;
    Response.Health = Pawn.Health;
    return Response;
}
}

MPlayerPawn::MPlayerPawn()
{
}

void MPlayerPawn::InitializeForLogin(uint32 InSceneId, uint32 InHealth, float InX, float InY, float InZ)
{
    SceneId = InSceneId;
    X = InX;
    Y = InY;
    Z = InZ;
    Health = InHealth;
    MarkPropertyDirty("SceneId");
    MarkPropertyDirty("X");
    MarkPropertyDirty("Y");
    MarkPropertyDirty("Z");
    MarkPropertyDirty("Health");
}

void MPlayerPawn::SyncFromPersistence(uint32 InSceneId, uint32 InHealth, float InX, float InY, float InZ)
{
    SceneId = InSceneId;
    X = InX;
    Y = InY;
    Z = InZ;
    Health = InHealth;
}

void MPlayerPawn::Spawn(uint32 InSceneId, uint32 InHealth, float InX, float InY, float InZ)
{
    SceneId = InSceneId;
    X = InX;
    Y = InY;
    Z = InZ;
    Health = InHealth;
    MarkPropertyDirty("SceneId");
    MarkPropertyDirty("X");
    MarkPropertyDirty("Y");
    MarkPropertyDirty("Z");
    MarkPropertyDirty("Health");
}

void MPlayerPawn::Despawn()
{
    SceneId = 0;
    MarkPropertyDirty("SceneId");
}

void MPlayerPawn::SetSceneId(uint32 InSceneId)
{
    SceneId = InSceneId;
    MarkPropertyDirty("SceneId");
}

void MPlayerPawn::SetLocation(float InX, float InY, float InZ)
{
    X = InX;
    Y = InY;
    Z = InZ;
    MarkPropertyDirty("X");
    MarkPropertyDirty("Y");
    MarkPropertyDirty("Z");
}

void MPlayerPawn::SetHealth(uint32 InHealth)
{
    Health = InHealth;
    MarkPropertyDirty("Health");
}

bool MPlayerPawn::IsSpawned() const
{
    return SceneId != 0;
}

MFuture<TResult<FPlayerQueryPawnResponse, FAppError>> MPlayerPawn::PlayerQueryPawn(
    const FPlayerQueryPawnRequest& /*Request*/)
{
    const MPlayer* Player = dynamic_cast<const MPlayer*>(GetOuter());
    if (!Player)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerQueryPawnResponse>(
            "player_owner_missing",
            "PlayerQueryPawn");
    }

    if (!IsSpawned())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerQueryPawnResponse>(
            "player_pawn_not_spawned",
            "PlayerQueryPawn");
    }

    return MServerCallAsyncSupport::MakeSuccessFuture(BuildPawnStateResponse<FPlayerQueryPawnResponse>(Player->PlayerId, *this));
}
