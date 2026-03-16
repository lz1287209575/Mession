#include "Common/ReflectionExample.h"

#include <algorithm>

void MCharacter::TakeDamage(float Damage)
{
    Health -= Damage;
    if (Health <= 0.0f)
    {
        Health = 0.0f;
        bIsAlive = false;
    }
}

void MCharacter::Heal(float Amount)
{
    Health += Amount;
    if (Health > MaxHealth)
    {
        Health = MaxHealth;
    }

    if (Health > 0.0f)
    {
        bIsAlive = true;
    }
}

void MCharacter::MoveTo(const SVector& NewLocation)
{
    Location = NewLocation;
}

void MCharacter::LevelUp()
{
    ++Level;
    MaxHealth += 10.0f;
    Health = MaxHealth;
}

void MCharacter::Rename(const FString& NewName)
{
    Name = NewName;
}

int32 MCharacter::GetGoldAmount() const
{
    return Gold;
}

bool MCharacter::IsAlive() const
{
    return bIsAlive;
}

void MCharacter::Tick(float)
{
}

void MCharacter::BeginPlay()
{
}

void MHero::ServerRpc_AddStats(int32 LevelDelta, float HealthDelta)
{
    Level += LevelDelta;
    Health += HealthDelta;
}

bool MHero::ServerRpc_AddStats_Validate(int32 LevelDelta, float HealthDelta) const
{
    return LevelDelta >= 0 && LevelDelta <= 100 && HealthDelta >= -1000.0f && HealthDelta <= 1000.0f;
}

void MPlayerData::AddFriend(uint64 FriendId)
{
    if (!HasFriend(FriendId))
    {
        FriendsList.push_back(FriendId);
    }
}

void MPlayerData::RemoveFriend(uint64 FriendId)
{
    FriendsList.erase(
        std::remove(FriendsList.begin(), FriendsList.end(), FriendId),
        FriendsList.end());
}

bool MPlayerData::HasFriend(uint64 FriendId) const
{
    return std::find(FriendsList.begin(), FriendsList.end(), FriendId) != FriendsList.end();
}
