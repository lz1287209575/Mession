#pragma once

#include "Common/Runtime/MLib.h"
#include "Protocol/Messages/Combat/CombatSceneMessages.h"

struct FSkillExecutionContext
{
    uint64 CasterPlayerId = 0;
    uint64 PrimaryTargetPlayerId = 0;
    uint32 SceneId = 0;

    const FSceneCombatAvatarSnapshot* CasterSnapshot = nullptr;
    FSceneCombatAvatarSnapshot* PrimaryTargetSnapshot = nullptr;
    TVector<FSceneCombatAvatarSnapshot*> SelectedTargets;

    uint32 AppliedDamage = 0;
    bool bFinished = false;
    bool bFailed = false;
    MString FailReason;
};
