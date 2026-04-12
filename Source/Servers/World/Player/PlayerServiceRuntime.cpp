#include "Servers/World/Player/PlayerService.h"

#include "Common/Runtime/Persistence/PersistenceSubsystem.h"
#include "Common/Runtime/StringUtils.h"
#include "Servers/World/Player/Player.h"
#include "Servers/World/Player/PlayerController.h"
#include "Servers/World/Player/PlayerProfile.h"

void MPlayerService::Initialize(MWorldServer* InWorldServer)
{
    WorldServer = InWorldServer;
    if (!PlayerRootResolver)
    {
        PlayerRootResolver = std::make_unique<FPlayerObjectCallRootResolver>(&OnlinePlayers);
    }
    else
    {
        PlayerRootResolver->SetOnlinePlayers(&OnlinePlayers);
    }
}

const TMap<uint64, MPlayer*>& MPlayerService::GetOnlinePlayers() const
{
    return OnlinePlayers;
}

void MPlayerService::FlushPersistence() const
{
    if (!WorldServer)
    {
        return;
    }

    for (const auto& [PlayerId, Player] : OnlinePlayers)
    {
        (void)PlayerId;
        (void)WorldServer->GetPersistence().EnqueueRootIfDirty(Player);
    }

    (void)WorldServer->GetPersistence().Flush(64);
}

void MPlayerService::ShutdownPlayers()
{
    if (!WorldServer)
    {
        return;
    }

    for (auto& [PlayerId, Player] : OnlinePlayers)
    {
        (void)PlayerId;
        DestroyMObject(Player);
    }

    OnlinePlayers.clear();
}

MPlayer* MPlayerService::FindPlayer(uint64 PlayerId) const
{
    if (!WorldServer)
    {
        return nullptr;
    }

    const auto It = OnlinePlayers.find(PlayerId);
    return It != OnlinePlayers.end() ? It->second : nullptr;
}

MPlayerController* MPlayerService::FindController(uint64 PlayerId) const
{
    MPlayer* Player = FindPlayer(PlayerId);
    return Player ? Player->GetController() : nullptr;
}

MPlayerPawn* MPlayerService::FindPawn(uint64 PlayerId) const
{
    MPlayer* Player = FindPlayer(PlayerId);
    return Player ? Player->GetPawn() : nullptr;
}

MPlayerProfile* MPlayerService::FindProfile(uint64 PlayerId) const
{
    MPlayer* Player = FindPlayer(PlayerId);
    return Player ? Player->GetProfile() : nullptr;
}

MPlayerInventory* MPlayerService::FindInventory(uint64 PlayerId) const
{
    MPlayerProfile* Profile = FindProfile(PlayerId);
    return Profile ? Profile->GetInventory() : nullptr;
}

MPlayerProgression* MPlayerService::FindProgression(uint64 PlayerId) const
{
    MPlayerProfile* Profile = FindProfile(PlayerId);
    return Profile ? Profile->GetProgression() : nullptr;
}

MFuture<TResult<FPlayerMoveResponse, FAppError>> MPlayerService::PlayerMove(const FPlayerMoveRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerMoveResponse>("player_id_required", "PlayerMove");
    }

    MPlayerController* Controller = FindController(Request.PlayerId);
    if (!Controller)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerMoveResponse>("player_controller_missing", "PlayerMove");
    }

    MFuture<TResult<FPlayerMoveResponse, FAppError>> Future = Controller->PlayerMove(Request);

    Future.Then([this, PlayerId = Request.PlayerId](MFuture<TResult<FPlayerMoveResponse, FAppError>> Completed)
    {
        try
        {
            const TResult<FPlayerMoveResponse, FAppError> Result = Completed.Get();
            if (Result.IsOk())
            {
                QueueScenePlayerUpdateNotify(PlayerId);
            }
        }
        catch (...)
        {
        }
    });

    return Future;
}

MPlayer* MPlayerService::FindOrCreatePlayer(uint64 PlayerId)
{
    if (MPlayer* Player = FindPlayer(PlayerId))
    {
        return Player;
    }

    MPlayer* Player = NewMObject<MPlayer>(WorldServer, "Player_" + MStringUtil::ToString(PlayerId));
    OnlinePlayers[PlayerId] = Player;
    return Player;
}

void MPlayerService::RemovePlayer(uint64 PlayerId)
{
    auto It = OnlinePlayers.find(PlayerId);
    if (It == OnlinePlayers.end())
    {
        return;
    }

    MPlayer* Player = It->second;
    OnlinePlayers.erase(It);
    DestroyMObject(Player);
}
