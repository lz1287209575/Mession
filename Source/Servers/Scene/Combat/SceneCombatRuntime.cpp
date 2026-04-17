#include "Servers/Scene/Combat/SceneCombatRuntime.h"

#include "Servers/Scene/Combat/Monster.h"
#include "Servers/Scene/Combat/MonsterConfig.h"
#include "Servers/Scene/Combat/MonsterManager.h"

void MSceneCombatRuntime::Initialize(const MSkillCatalog* InSkillCatalog, MMonsterManager* InMonsterManager)
{
    SkillCatalog = InSkillCatalog;
    MonsterManager = InMonsterManager;
}

bool MSceneCombatRuntime::SpawnAvatar(
    const FSceneCombatAvatarSnapshot& Avatar,
    uint64& OutCombatEntityId,
    MString& OutError)
{
    if (Avatar.PlayerId == 0)
    {
        OutError = "player_id_required";
        return false;
    }

    if (Avatar.SceneId == 0)
    {
        OutError = "scene_id_required";
        return false;
    }

    const auto ExistingEntityIt = CombatEntityIdByPlayerId.find(Avatar.PlayerId);
    if (ExistingEntityIt != CombatEntityIdByPlayerId.end())
    {
        const auto StateIt = AvatarsByCombatEntityId.find(ExistingEntityIt->second);
        if (StateIt == AvatarsByCombatEntityId.end())
        {
            OutError = "scene_combat_avatar_index_corrupted";
            return false;
        }

        if (StateIt->second.Snapshot.SceneId != 0 && StateIt->second.Snapshot.SceneId != Avatar.SceneId)
        {
            RemoveUnitFromSceneIndex(StateIt->second.Snapshot.SceneId, StateIt->second.CombatEntityId);
            IndexUnitInScene(Avatar.SceneId, StateIt->second.CombatEntityId);
        }

        StateIt->second.Unit = FCombatUnitRef::MakePlayer(StateIt->second.CombatEntityId, Avatar.PlayerId);
        StateIt->second.Snapshot = Avatar;
        StateIt->second.Snapshot.UnitKind = ECombatUnitKind::Player;
        OutCombatEntityId = StateIt->second.CombatEntityId;
        return true;
    }

    FSceneCombatAvatarState State;
    State.CombatEntityId = NextCombatEntityId++;
    State.Unit = FCombatUnitRef::MakePlayer(State.CombatEntityId, Avatar.PlayerId);
    State.Snapshot = Avatar;
    State.Snapshot.UnitKind = ECombatUnitKind::Player;
    OutCombatEntityId = State.CombatEntityId;
    AvatarsByCombatEntityId[State.CombatEntityId] = State;
    CombatEntityIdByPlayerId[Avatar.PlayerId] = State.CombatEntityId;
    IndexUnitInScene(Avatar.SceneId, State.CombatEntityId);
    return true;
}

bool MSceneCombatRuntime::SpawnMonster(
    const FSceneCombatMonsterSpawnParams& Params,
    FCombatUnitRef& OutUnit,
    MString& OutError)
{
    if (!MonsterManager)
    {
        OutError = "monster_manager_missing";
        return false;
    }

    return MonsterManager->SpawnMonster(NextCombatEntityId++, Params, OutUnit, OutError);
}

bool MSceneCombatRuntime::SpawnMonster(
    const MMonsterConfig& Config,
    FCombatUnitRef& OutUnit,
    MString& OutError)
{
    if (!MonsterManager)
    {
        OutError = "monster_manager_missing";
        return false;
    }

    return MonsterManager->SpawnMonster(NextCombatEntityId++, Config, OutUnit, OutError);
}

bool MSceneCombatRuntime::DespawnAvatar(uint64 PlayerId, uint64* OutCombatEntityId, MString& OutError)
{
    if (PlayerId == 0)
    {
        OutError = "player_id_required";
        return false;
    }

    const auto EntityIt = CombatEntityIdByPlayerId.find(PlayerId);
    if (EntityIt == CombatEntityIdByPlayerId.end())
    {
        OutError = "scene_combat_avatar_not_found";
        return false;
    }

    return DespawnUnit(FCombatUnitRef::MakePlayer(EntityIt->second, PlayerId), OutCombatEntityId, OutError);
}

bool MSceneCombatRuntime::DespawnUnit(const FCombatUnitRef& Unit, uint64* OutCombatEntityId, MString& OutError)
{
    if (Unit.IsMonster())
    {
        if (!MonsterManager)
        {
            OutError = "monster_manager_missing";
            return false;
        }

        return MonsterManager->DespawnMonster(Unit, OutCombatEntityId, OutError);
    }

    const FSceneCombatAvatarState* ExistingState = FindUnit(Unit);
    if (ExistingState)
    {
        const uint64 CombatEntityId = ExistingState->CombatEntityId;
        const uint32 SceneId = ExistingState->Snapshot.SceneId;
        const uint64 PlayerId = ExistingState->Unit.PlayerId;

        if (OutCombatEntityId)
        {
            *OutCombatEntityId = CombatEntityId;
        }

        RemoveUnitFromSceneIndex(SceneId, CombatEntityId);
        AvatarsByCombatEntityId.erase(CombatEntityId);
        if (PlayerId != 0)
        {
            CombatEntityIdByPlayerId.erase(PlayerId);
        }
        return true;
    }

    if (MonsterManager && Unit.CombatEntityId != 0)
    {
        return MonsterManager->DespawnMonster(Unit, OutCombatEntityId, OutError);
    }

    OutError = "scene_combat_unit_not_found";
    return false;
}

bool MSceneCombatRuntime::CastSkill(
    const FSceneCastSkillRequest& Request,
    FSceneCastSkillResponse& OutResponse,
    MString& OutError)
{
    FCombatUnitRef CasterUnit = Request.CasterUnit;
    if (!CasterUnit.IsValid() && Request.CasterPlayerId != 0)
    {
        CasterUnit = ResolveLegacyPlayerUnit(Request.CasterPlayerId);
    }

    FCombatUnitRef TargetUnit = Request.TargetUnit;
    if (!TargetUnit.IsValid() && Request.TargetPlayerId != 0)
    {
        TargetUnit = ResolveLegacyPlayerUnit(Request.TargetPlayerId);
    }

    FResolvedCombatUnit Caster;
    if (!ResolveUnitHandle(CasterUnit, Caster))
    {
        OutError = "scene_combat_caster_not_found";
        return false;
    }

    FResolvedCombatUnit Target;
    if (!ResolveUnitHandle(TargetUnit, Target))
    {
        OutError = "scene_combat_target_not_found";
        return false;
    }

    if (!Caster.Snapshot || !Caster.MutableSnapshot || !Target.Snapshot || !Target.MutableSnapshot)
    {
        OutError = "scene_combat_snapshot_missing";
        return false;
    }

    if (Caster.Snapshot->SceneId == 0 || Caster.Snapshot->SceneId != Target.Snapshot->SceneId)
    {
        OutError = "combat_scene_mismatch";
        return false;
    }

    if (!SkillCatalog)
    {
        OutError = "skill_catalog_missing";
        return false;
    }

    const FSkillSpec* SkillSpec = SkillCatalog->FindSkill(Request.SkillId);
    if (!SkillSpec)
    {
        OutError = "skill_not_found";
        return false;
    }

    if (SkillSpec->TargetType == ESkillTargetType::Self && Caster.CombatEntityId != Target.CombatEntityId)
    {
        OutError = "skill_target_invalid";
        return false;
    }

    if (!SkillSpec->bCanTargetSelf && Caster.CombatEntityId == Target.CombatEntityId)
    {
        OutError = "skill_target_invalid";
        return false;
    }

    FSkillExecutionContext ExecutionContext;
    ExecutionContext.CasterUnit = Caster.Unit;
    ExecutionContext.PrimaryTargetUnit = Target.Unit;
    ExecutionContext.SceneId = Caster.Snapshot->SceneId;
    ExecutionContext.CasterSnapshot = Caster.Snapshot;
    ExecutionContext.PrimaryTargetSnapshot = Target.MutableSnapshot;

    if (!SkillExecutor.ExecuteSkill(*SkillSpec, ExecutionContext, OutError))
    {
        return false;
    }

    if (Caster.Monster)
    {
        Caster.Monster->SyncCombatStateFromSnapshot();
    }
    if (Target.Monster)
    {
        Target.Monster->SyncCombatStateFromSnapshot();
    }

    OutResponse.bAccepted = true;
    OutResponse.SceneId = Caster.Snapshot->SceneId;
    OutResponse.CasterUnit = Caster.Unit;
    OutResponse.TargetUnit = Target.Unit;
    OutResponse.CasterPlayerId = Caster.Unit.PlayerId;
    OutResponse.TargetPlayerId = Target.Unit.PlayerId;
    OutResponse.SkillId = SkillSpec->SkillId;
    OutResponse.AppliedDamage = ExecutionContext.AppliedDamage;
    OutResponse.TargetHealth = Target.MutableSnapshot->CurrentHealth;
    OutResponse.bTargetDefeated = (Target.MutableSnapshot->CurrentHealth == 0);
    if (OutResponse.bTargetDefeated && Target.Monster)
    {
        OutResponse.ExperienceReward = Target.Monster->GetExperienceReward();
        OutResponse.GoldReward = Target.Monster->GetGoldReward();
    }
    return true;
}

const FSceneCombatAvatarState* MSceneCombatRuntime::FindAvatar(uint64 PlayerId) const
{
    return FindUnit(ResolveLegacyPlayerUnit(PlayerId));
}

const FSceneCombatAvatarState* MSceneCombatRuntime::FindCombatUnit(const FCombatUnitRef& Unit) const
{
    return FindUnit(Unit);
}

TVector<FCombatUnitRef> MSceneCombatRuntime::ListUnitsInScene(uint32 SceneId, ECombatUnitKind FilterKind) const
{
    TVector<FCombatUnitRef> Units;

    if (FilterKind == ECombatUnitKind::Unknown || FilterKind == ECombatUnitKind::Player)
    {
        const auto SceneIt = PlayerCombatEntityIdsBySceneId.find(SceneId);
        if (SceneIt != PlayerCombatEntityIdsBySceneId.end())
        {
            for (uint64 CombatEntityId : SceneIt->second)
            {
                const auto UnitIt = AvatarsByCombatEntityId.find(CombatEntityId);
                if (UnitIt != AvatarsByCombatEntityId.end())
                {
                    Units.push_back(UnitIt->second.Unit);
                }
            }
        }
    }

    if ((FilterKind == ECombatUnitKind::Unknown || FilterKind == ECombatUnitKind::Monster) && MonsterManager)
    {
        TVector<FCombatUnitRef> MonsterUnits = MonsterManager->ListMonstersInScene(SceneId);
        Units.insert(Units.end(), MonsterUnits.begin(), MonsterUnits.end());
    }

    return Units;
}

bool MSceneCombatRuntime::ArePlayersInSameScene(uint64 PlayerA, uint64 PlayerB, uint32& OutSceneId) const
{
    const FSceneCombatAvatarState* AvatarA = FindAvatar(PlayerA);
    const FSceneCombatAvatarState* AvatarB = FindAvatar(PlayerB);
    if (!AvatarA || !AvatarB)
    {
        return false;
    }

    if (AvatarA->Snapshot.SceneId == 0 || AvatarA->Snapshot.SceneId != AvatarB->Snapshot.SceneId)
    {
        return false;
    }

    OutSceneId = AvatarA->Snapshot.SceneId;
    return true;
}

const FSceneCombatAvatarState* MSceneCombatRuntime::FindUnit(const FCombatUnitRef& Unit) const
{
    if (Unit.CombatEntityId != 0)
    {
        const auto It = AvatarsByCombatEntityId.find(Unit.CombatEntityId);
        if (It == AvatarsByCombatEntityId.end())
        {
            return nullptr;
        }

        if (Unit.UnitKind != ECombatUnitKind::Unknown && It->second.Unit.UnitKind != Unit.UnitKind)
        {
            return nullptr;
        }

        if (Unit.PlayerId != 0 && It->second.Unit.PlayerId != Unit.PlayerId)
        {
            return nullptr;
        }

        return &It->second;
    }

    if (Unit.IsPlayer() && Unit.PlayerId != 0)
    {
        const auto EntityIt = CombatEntityIdByPlayerId.find(Unit.PlayerId);
        if (EntityIt == CombatEntityIdByPlayerId.end())
        {
            return nullptr;
        }

        const auto StateIt = AvatarsByCombatEntityId.find(EntityIt->second);
        return StateIt != AvatarsByCombatEntityId.end() ? &StateIt->second : nullptr;
    }

    return nullptr;
}

bool MSceneCombatRuntime::ResolveUnitHandle(const FCombatUnitRef& Unit, FResolvedCombatUnit& OutUnit) const
{
    OutUnit = FResolvedCombatUnit {};

    if (const FSceneCombatAvatarState* PlayerUnit = FindUnit(Unit))
    {
        OutUnit.Unit = PlayerUnit->Unit;
        OutUnit.CombatEntityId = PlayerUnit->CombatEntityId;
        OutUnit.Snapshot = &PlayerUnit->Snapshot;
        OutUnit.MutableSnapshot = const_cast<FSceneCombatAvatarSnapshot*>(&PlayerUnit->Snapshot);
        return true;
    }

    if (!MonsterManager)
    {
        return false;
    }

    MMonster* Monster = MonsterManager->FindMonster(Unit);
    if (!Monster)
    {
        return false;
    }

    FSceneCombatAvatarSnapshot* MonsterSnapshot = Monster->GetMutableCombatSnapshot();
    if (!MonsterSnapshot)
    {
        return false;
    }

    OutUnit.Unit = Monster->GetUnitRef();
    OutUnit.CombatEntityId = Monster->GetCombatEntityId();
    OutUnit.Snapshot = MonsterSnapshot;
    OutUnit.MutableSnapshot = MonsterSnapshot;
    OutUnit.Monster = Monster;
    return true;
}

FCombatUnitRef MSceneCombatRuntime::ResolveLegacyPlayerUnit(uint64 PlayerId) const
{
    if (PlayerId == 0)
    {
        return FCombatUnitRef {};
    }

    const auto It = CombatEntityIdByPlayerId.find(PlayerId);
    if (It == CombatEntityIdByPlayerId.end())
    {
        return FCombatUnitRef::MakePlayer(0, PlayerId);
    }

    return FCombatUnitRef::MakePlayer(It->second, PlayerId);
}

void MSceneCombatRuntime::IndexUnitInScene(uint32 SceneId, uint64 CombatEntityId)
{
    if (SceneId == 0 || CombatEntityId == 0)
    {
        return;
    }

    PlayerCombatEntityIdsBySceneId[SceneId].insert(CombatEntityId);
}

void MSceneCombatRuntime::RemoveUnitFromSceneIndex(uint32 SceneId, uint64 CombatEntityId)
{
    if (SceneId == 0 || CombatEntityId == 0)
    {
        return;
    }

    const auto SceneIt = PlayerCombatEntityIdsBySceneId.find(SceneId);
    if (SceneIt == PlayerCombatEntityIdsBySceneId.end())
    {
        return;
    }

    SceneIt->second.erase(CombatEntityId);
    if (SceneIt->second.empty())
    {
        PlayerCombatEntityIdsBySceneId.erase(SceneIt);
    }
}

void MSceneCombatRuntime::Tick(float DeltaTime)
{
    (void)DeltaTime;
}
