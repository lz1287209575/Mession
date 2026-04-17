#include "Servers/World/Player/PlayerManager.h"

#include "Common/Runtime/StringUtils.h"
#include "Servers/World/Player/Player.h"
#include "Servers/World/Player/PlayerCombatProfile.h"
#include "Servers/World/Player/PlayerController.h"
#include "Servers/World/Player/PlayerInventory.h"
#include "Servers/World/Player/PlayerPawn.h"
#include "Servers/World/Player/PlayerProfile.h"
#include "Servers/World/Player/PlayerProgression.h"
#include "Servers/World/WorldServer.h"

void MPlayerManager::Initialize(MWorldServer* InWorldServer)
{
    WorldServer = InWorldServer;
}

const TMap<uint64, MPlayer*>& MPlayerManager::GetOnlinePlayers() const
{
    return OnlinePlayers;
}

void MPlayerManager::VisitOnlinePlayers(const TFunction<void(uint64 PlayerId, MPlayer* Player)>& Visitor) const
{
    if (!Visitor)
    {
        return;
    }

    for (const auto& [PlayerId, Player] : OnlinePlayers)
    {
        Visitor(PlayerId, Player);
    }
}

void MPlayerManager::VisitNotifiablePlayersInScene(
    uint32 SceneId,
    const TFunction<void(uint64 PlayerId, MPlayer* Player)>& Visitor) const
{
    if (SceneId == 0 || !Visitor)
    {
        return;
    }

    VisitOnlinePlayers(
        [SceneId, &Visitor](uint64 PlayerId, MPlayer* Player)
        {
            if (!Player || !Player->CanReceiveSceneNotify())
            {
                return;
            }

            if (Player->ResolveCurrentSceneId() != SceneId)
            {
                return;
            }

            Visitor(PlayerId, Player);
        });
}

void MPlayerManager::QueueClientNotifyToPlayer(uint64 PlayerId, uint16 FunctionId, const TByteArray& Payload) const
{
    if (!WorldServer || PlayerId == 0 || FunctionId == 0)
    {
        return;
    }

    MPlayer* Player = FindPlayer(PlayerId);
    if (!Player || !Player->CanReceiveSceneNotify())
    {
        return;
    }

    const uint64 GatewayConnectionId = Player->ResolveGatewayConnectionId();
    if (GatewayConnectionId == 0)
    {
        return;
    }

    WorldServer->QueueClientNotify(GatewayConnectionId, FunctionId, Payload);
}

void MPlayerManager::QueueClientNotifyToPlayers(
    const TVector<uint64>& PlayerIds,
    uint16 FunctionId,
    const TByteArray& Payload) const
{
    for (uint64 PlayerId : PlayerIds)
    {
        QueueClientNotifyToPlayer(PlayerId, FunctionId, Payload);
    }
}

void MPlayerManager::FlushPersistence() const
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

void MPlayerManager::ShutdownPlayers()
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

MPlayer* MPlayerManager::FindPlayer(uint64 PlayerId) const
{
    if (!WorldServer)
    {
        return nullptr;
    }

    const auto It = OnlinePlayers.find(PlayerId);
    return It != OnlinePlayers.end() ? It->second : nullptr;
}

MPlayer* MPlayerManager::FindOrCreatePlayer(uint64 PlayerId)
{
    if (MPlayer* Player = FindPlayer(PlayerId))
    {
        return Player;
    }

    MPlayer* Player = NewMObject<MPlayer>(WorldServer, "Player_" + MStringUtil::ToString(PlayerId));
    OnlinePlayers[PlayerId] = Player;
    return Player;
}

void MPlayerManager::RemovePlayer(uint64 PlayerId)
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

MPlayerController* MPlayerManager::FindController(uint64 PlayerId) const
{
    MPlayer* Player = FindPlayer(PlayerId);
    return Player ? Player->GetController() : nullptr;
}

MPlayerPawn* MPlayerManager::FindPawn(uint64 PlayerId) const
{
    MPlayer* Player = FindPlayer(PlayerId);
    return Player ? Player->GetPawn() : nullptr;
}

MPlayerProfile* MPlayerManager::FindProfile(uint64 PlayerId) const
{
    MPlayer* Player = FindPlayer(PlayerId);
    return Player ? Player->GetProfile() : nullptr;
}

MPlayerInventory* MPlayerManager::FindInventory(uint64 PlayerId) const
{
    MPlayerProfile* Profile = FindProfile(PlayerId);
    return Profile ? Profile->GetInventory() : nullptr;
}

MPlayerProgression* MPlayerManager::FindProgression(uint64 PlayerId) const
{
    MPlayerProfile* Profile = FindProfile(PlayerId);
    return Profile ? Profile->GetProgression() : nullptr;
}

MPlayerCombatProfile* MPlayerManager::FindCombatProfile(uint64 PlayerId) const
{
    MPlayer* Player = FindPlayer(PlayerId);
    return Player ? Player->GetCombatProfile() : nullptr;
}
