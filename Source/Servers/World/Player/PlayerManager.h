#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"

class MPlayer;
class MPlayerCombatProfile;
class MPlayerController;
class MPlayerInventory;
class MPlayerPawn;
class MPlayerProfile;
class MPlayerProgression;
class MWorldServer;

MCLASS(Type=Object)
class MPlayerManager : public MObject
{
public:
    MGENERATED_BODY(MPlayerManager, MObject, 0)
public:
    void Initialize(MWorldServer* InWorldServer);

    const TMap<uint64, MPlayer*>& GetOnlinePlayers() const;
    void VisitOnlinePlayers(const TFunction<void(uint64 PlayerId, MPlayer* Player)>& Visitor) const;
    void VisitNotifiablePlayersInScene(
        uint32 SceneId,
        const TFunction<void(uint64 PlayerId, MPlayer* Player)>& Visitor) const;
    void QueueClientNotifyToPlayer(uint64 PlayerId, uint16 FunctionId, const TByteArray& Payload) const;
    void QueueClientNotifyToPlayers(const TVector<uint64>& PlayerIds, uint16 FunctionId, const TByteArray& Payload) const;
    void FlushPersistence() const;
    void ShutdownPlayers();

    MPlayer* FindPlayer(uint64 PlayerId) const;
    MPlayer* FindOrCreatePlayer(uint64 PlayerId);
    void RemovePlayer(uint64 PlayerId);

    MPlayerController* FindController(uint64 PlayerId) const;
    MPlayerPawn* FindPawn(uint64 PlayerId) const;
    MPlayerProfile* FindProfile(uint64 PlayerId) const;
    MPlayerInventory* FindInventory(uint64 PlayerId) const;
    MPlayerProgression* FindProgression(uint64 PlayerId) const;
    MPlayerCombatProfile* FindCombatProfile(uint64 PlayerId) const;

private:
    MWorldServer* WorldServer = nullptr;
    TMap<uint64, MPlayer*> OnlinePlayers;
};
