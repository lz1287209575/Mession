#pragma once

#include "Common/Runtime/MLib.h"
#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Servers/Scene/Combat/SkillExecutionContext.h"
#include "Servers/Scene/Combat/SkillCatalog.h"
#include "Servers/Scene/Combat/SkillStepExecutor.h"

struct FSceneCombatAvatarState
{
    uint64 CombatEntityId = 0;
    FSceneCombatAvatarSnapshot Snapshot;
};

class MSceneCombatRuntime
{
public:
    void Initialize(const MSkillCatalog* InSkillCatalog);

    bool SpawnAvatar(const FSceneCombatAvatarSnapshot& Avatar, uint64& OutCombatEntityId, MString& OutError);
    bool DespawnAvatar(uint64 PlayerId, uint64* OutCombatEntityId, MString& OutError);
    bool CastSkill(const FSceneCastSkillRequest& Request, FSceneCastSkillResponse& OutResponse, MString& OutError);

    const FSceneCombatAvatarState* FindAvatar(uint64 PlayerId) const;
    bool ArePlayersInSameScene(uint64 PlayerA, uint64 PlayerB, uint32& OutSceneId) const;

    void Tick(float DeltaTime);

private:
    const MSkillCatalog* SkillCatalog = nullptr;
    MSkillStepExecutor SkillExecutor;
    uint64 NextCombatEntityId = 1;
    TMap<uint64, FSceneCombatAvatarState> AvatarsByPlayerId;
};
