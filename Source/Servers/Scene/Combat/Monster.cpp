#include "Servers/Scene/Combat/Monster.h"

#include "Servers/Scene/Combat/SceneCombatRuntime.h"

MMonster::MMonster()
{
    CombatProfile = CreateDefaultSubObject<MMonsterCombatProfile>(this, "CombatProfile");
    CombatState = CreateDefaultSubObject<MMonsterCombatState>(this, "CombatState");
}

void MMonster::InitializeForSpawn(
    uint64 InCombatEntityId,
    const FSceneCombatMonsterSpawnParams& Params)
{
    CombatEntityId = InCombatEntityId;

    if (CombatProfile)
    {
        CombatProfile->Initialize(
            Params.AttackPower,
            Params.DefensePower,
            Params.MaxHealth,
            Params.PrimarySkillId,
            Params.ExperienceReward,
            Params.GoldReward);
    }

    if (CombatState)
    {
        const uint32 InitialHealth = Params.CurrentHealth != 0 ? Params.CurrentHealth : Params.MaxHealth;
        CombatState->Initialize(
            Params.SceneId,
            Params.MonsterTemplateId,
            InitialHealth,
            Params.DebugName);
        CombatState->RefreshSnapshot(
            CombatProfile ? CombatProfile->MaxHealth : Params.MaxHealth,
            CombatProfile ? CombatProfile->BaseAttack : Params.AttackPower,
            CombatProfile ? CombatProfile->BaseDefense : Params.DefensePower,
            CombatProfile ? CombatProfile->PrimarySkillId : Params.PrimarySkillId);
    }
}

uint64 MMonster::GetCombatEntityId() const
{
    return CombatEntityId;
}

FCombatUnitRef MMonster::GetUnitRef() const
{
    return FCombatUnitRef::MakeMonster(CombatEntityId);
}

uint32 MMonster::GetSceneId() const
{
    return CombatState ? CombatState->SceneId : 0;
}

uint32 MMonster::GetMonsterTemplateId() const
{
    return CombatState ? CombatState->MonsterTemplateId : 0;
}

const MString& MMonster::GetDebugName() const
{
    static const MString Empty;
    return CombatState ? CombatState->DebugName : Empty;
}

uint32 MMonster::GetExperienceReward() const
{
    return CombatProfile ? CombatProfile->ExperienceReward : 0;
}

uint32 MMonster::GetGoldReward() const
{
    return CombatProfile ? CombatProfile->GoldReward : 0;
}

const FSceneCombatAvatarSnapshot& MMonster::GetCombatSnapshot() const
{
    static const FSceneCombatAvatarSnapshot Empty;
    return CombatState ? CombatState->GetSnapshot() : Empty;
}

FSceneCombatAvatarSnapshot* MMonster::GetMutableCombatSnapshot()
{
    if (!CombatState)
    {
        return nullptr;
    }

    CombatState->RefreshSnapshot(
        CombatProfile ? CombatProfile->MaxHealth : 1,
        CombatProfile ? CombatProfile->BaseAttack : 0,
        CombatProfile ? CombatProfile->BaseDefense : 0,
        CombatProfile ? CombatProfile->PrimarySkillId : 1001);
    return CombatState->GetMutableSnapshot();
}

void MMonster::SyncCombatStateFromSnapshot()
{
    if (CombatState)
    {
        CombatState->SyncFromSnapshot();
    }
}

MMonsterCombatProfile* MMonster::GetCombatProfile() const
{
    return CombatProfile;
}

MMonsterCombatState* MMonster::GetCombatState() const
{
    return CombatState;
}

void MMonster::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const
{
    MObject::VisitReferencedObjects(Visitor);
    if (Visitor)
    {
        Visitor(CombatProfile);
        Visitor(CombatState);
    }
}
