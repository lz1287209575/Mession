#include "Servers/World/Player/PlayerService.h"

#include "Servers/World/Player/PlayerCombatProfile.h"
#include "Servers/World/Player/PlayerController.h"
#include "Servers/World/Player/PlayerManager.h"

void MPlayerService::Initialize(MWorldServer* InWorldServer)
{
    WorldServer = InWorldServer;
    PlayerManager = WorldServer ? WorldServer->GetPlayerManager() : nullptr;
    if (!PlayerCommandRuntime && WorldServer)
    {
        PlayerCommandRuntime = std::make_unique<MPlayerCommandRuntime>(WorldServer->GetTaskRunner());
    }
    if (!PlayerRootResolver)
    {
        PlayerRootResolver = std::make_unique<FPlayerObjectCallRootResolver>(
            PlayerManager ? &PlayerManager->GetOnlinePlayers() : nullptr);
    }
    else
    {
        PlayerRootResolver->SetOnlinePlayers(PlayerManager ? &PlayerManager->GetOnlinePlayers() : nullptr);
    }
}

const TMap<uint64, MPlayer*>& MPlayerService::GetOnlinePlayers() const
{
    static const TMap<uint64, MPlayer*> EmptyPlayers;
    return PlayerManager ? PlayerManager->GetOnlinePlayers() : EmptyPlayers;
}

void MPlayerService::FlushPersistence() const
{
    if (!PlayerManager)
    {
        return;
    }
    PlayerManager->FlushPersistence();
}

void MPlayerService::ShutdownPlayers()
{
    if (!PlayerManager)
    {
        return;
    }
    PlayerManager->ShutdownPlayers();
}

MPlayer* MPlayerService::FindPlayer(uint64 PlayerId) const
{
    return PlayerManager ? PlayerManager->FindPlayer(PlayerId) : nullptr;
}

MPlayerController* MPlayerService::FindController(uint64 PlayerId) const
{
    return PlayerManager ? PlayerManager->FindController(PlayerId) : nullptr;
}

MPlayerPawn* MPlayerService::FindPawn(uint64 PlayerId) const
{
    return PlayerManager ? PlayerManager->FindPawn(PlayerId) : nullptr;
}

MPlayerProfile* MPlayerService::FindProfile(uint64 PlayerId) const
{
    return PlayerManager ? PlayerManager->FindProfile(PlayerId) : nullptr;
}

MPlayerInventory* MPlayerService::FindInventory(uint64 PlayerId) const
{
    return PlayerManager ? PlayerManager->FindInventory(PlayerId) : nullptr;
}

MPlayerProgression* MPlayerService::FindProgression(uint64 PlayerId) const
{
    return PlayerManager ? PlayerManager->FindProgression(PlayerId) : nullptr;
}

MPlayerCombatProfile* MPlayerService::FindCombatProfile(uint64 PlayerId) const
{
    return PlayerManager ? PlayerManager->FindCombatProfile(PlayerId) : nullptr;
}

MFuture<TResult<FPlayerMoveResponse, FAppError>> MPlayerService::PlayerMove(const FPlayerMoveRequest& Request)
{
    return DispatchPlayerComponentWithSceneUpdate<MPlayerController, FPlayerMoveResponse>(
        Request,
        &MPlayerService::FindController,
        &MPlayerController::PlayerMove,
        "player_controller_missing",
        "PlayerMove");
}

MPlayer* MPlayerService::FindOrCreatePlayer(uint64 PlayerId)
{
    return PlayerManager ? PlayerManager->FindOrCreatePlayer(PlayerId) : nullptr;
}

void MPlayerService::RemovePlayer(uint64 PlayerId)
{
    if (!PlayerManager)
    {
        return;
    }
    PlayerManager->RemovePlayer(PlayerId);
}
