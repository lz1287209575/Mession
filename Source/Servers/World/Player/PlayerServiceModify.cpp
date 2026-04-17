#include "Servers/World/Player/PlayerService.h"

#include "Servers/World/Player/PlayerCombatProfile.h"
#include "Servers/World/Player/PlayerInventory.h"
#include "Servers/World/Player/PlayerProgression.h"

MFuture<TResult<FPlayerChangeGoldResponse, FAppError>> MPlayerService::PlayerChangeGold(
    const FPlayerChangeGoldRequest& Request)
{
    return DispatchPlayerComponent<MPlayerInventory, FPlayerChangeGoldResponse>(
        Request,
        &MPlayerService::FindInventory,
        &MPlayerInventory::PlayerChangeGold,
        "player_inventory_missing",
        "PlayerChangeGold");
}

MFuture<TResult<FPlayerEquipItemResponse, FAppError>> MPlayerService::PlayerEquipItem(
    const FPlayerEquipItemRequest& Request)
{
    return DispatchPlayerComponent<MPlayerInventory, FPlayerEquipItemResponse>(
        Request,
        &MPlayerService::FindInventory,
        &MPlayerInventory::PlayerEquipItem,
        "player_inventory_missing",
        "PlayerEquipItem");
}

MFuture<TResult<FPlayerGrantExperienceResponse, FAppError>> MPlayerService::PlayerGrantExperience(
    const FPlayerGrantExperienceRequest& Request)
{
    return DispatchPlayerComponent<MPlayerProgression, FPlayerGrantExperienceResponse>(
        Request,
        &MPlayerService::FindProgression,
        &MPlayerProgression::PlayerGrantExperience,
        "player_progression_missing",
        "PlayerGrantExperience");
}

MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> MPlayerService::PlayerModifyHealth(
    const FPlayerModifyHealthRequest& Request)
{
    return DispatchPlayerComponentWithSceneUpdate<MPlayerProgression, FPlayerModifyHealthResponse>(
        Request,
        &MPlayerService::FindProgression,
        &MPlayerProgression::PlayerModifyHealth,
        "player_progression_missing",
        "PlayerModifyHealth");
}

MFuture<TResult<FPlayerSetPrimarySkillResponse, FAppError>> MPlayerService::PlayerSetPrimarySkill(
    const FPlayerSetPrimarySkillRequest& Request)
{
    return DispatchPlayerComponent<MPlayerCombatProfile, FPlayerSetPrimarySkillResponse>(
        Request,
        &MPlayerService::FindCombatProfile,
        &MPlayerCombatProfile::PlayerSetPrimarySkill,
        "player_combat_profile_missing",
        "PlayerSetPrimarySkill");
}
