#include "NetDriver/ReflectionExample.h"
#include "Common/Logger.h"

// ===========================
// MCharacter 实现
// ===========================

void MCharacter::TakeDamage(float Damage)
{
    Health -= Damage;
    if (Health <= 0.0f)
    {
        Health = 0.0f;
        bIsAlive = false;
        LOG_INFO("%s died!", Name.c_str());
    }
}

void MCharacter::Heal(float Amount)
{
    if (!bIsAlive)
    {
        return;
    }

    Health += Amount;
    if (Health > MaxHealth)
    {
        Health = MaxHealth;
    }
}

void MCharacter::MoveTo(const SVector& NewLocation)
{
    Location = NewLocation;
    LOG_DEBUG("%s moved to (%.2f, %.2f, %.2f)",
              Name.c_str(), Location.X, Location.Y, Location.Z);
}

void MCharacter::LevelUp()
{
    Level++;
    MaxHealth += 20.0f;
    Health = MaxHealth;
    LOG_INFO("%s leveled up! Now level %d", Name.c_str(), Level);
}

void MCharacter::Tick(float DeltaTime)
{
    (void)DeltaTime;
}

void MCharacter::BeginPlay()
{
    LOG_INFO("%s spawned at level %d", Name.c_str(), Level);
}

// ===========================
// MHero 实现（包含嵌套结构体）
// ===========================

void MHero::ServerRpc_AddStats(int32 LevelDelta, float HealthDelta)
{
    Level += LevelDelta;
    Health += HealthDelta;
}

bool MHero::ServerRpc_AddStats_Validate(int32 LevelDelta, float HealthDelta) const
{
    // 简单校验：不允许负值或过大增益
    if (LevelDelta < 0 || HealthDelta < 0.0f)
    {
        return false;
    }
    if (LevelDelta > 100 || HealthDelta > 10000.0f)
    {
    return false;
    }
    return true;
}

// ===========================
// MPlayerData 实现
// ===========================

void MPlayerData::AddFriend(uint64 FriendId)
{
    if (!HasFriend(FriendId))
    {
        FriendsList.push_back(FriendId);
    }
}

void MPlayerData::RemoveFriend(uint64 FriendId)
{
    for (auto It = FriendsList.begin(); It != FriendsList.end(); ++It)
    {
        if (*It == FriendId)
        {
            FriendsList.erase(It);
            break;
        }
    }
}

bool MPlayerData::HasFriend(uint64 FriendId) const
{
    for (uint64 Id : FriendsList)
    {
        if (Id == FriendId)
        {
            return true;
        }
    }
    return false;
}
