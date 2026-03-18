#include "Common/ReflectionSmokeTypes.h"

void MReflectionSmokeCharacter::TakeDamage(float Damage)
{
    Health -= Damage;
    if (Health <= 0.0f)
    {
        Health = 0.0f;
        bIsAlive = false;
    }
}

void MReflectionSmokeCharacter::Heal(float Amount)
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

void MReflectionSmokeCharacter::MoveTo(const SVector& NewLocation)
{
    Location = NewLocation;
}

void MReflectionSmokeCharacter::LevelUp()
{
    ++Level;
    MaxHealth += 10.0f;
    Health = MaxHealth;
}

void MReflectionSmokeCharacter::Rename(const FString& NewName)
{
    Name = NewName;
}

int32 MReflectionSmokeCharacter::GetGoldAmount() const
{
    return Gold;
}

bool MReflectionSmokeCharacter::IsAlive() const
{
    return bIsAlive;
}

void MReflectionSmokeCharacter::Tick(float)
{
}

void MReflectionSmokeCharacter::BeginPlay()
{
}

void MReflectionSmokeHero::ServerRpc_AddStats(int32 LevelDelta, float HealthDelta)
{
    Level += LevelDelta;
    Health += HealthDelta;
}

bool MReflectionSmokeHero::ServerRpc_AddStats_Validate(int32 LevelDelta, float HealthDelta) const
{
    return LevelDelta >= 0 && LevelDelta <= 100 && HealthDelta >= -1000.0f && HealthDelta <= 1000.0f;
}

void MReflectionSmokePlayerData::AddFriend(uint64 FriendId)
{
    if (std::find(FriendsList.begin(), FriendsList.end(), FriendId) == FriendsList.end())
    {
        FriendsList.push_back(FriendId);
    }
}

void MReflectionSmokePlayerData::RemoveFriend(uint64 FriendId)
{
    FriendsList.erase(
        std::remove(FriendsList.begin(), FriendsList.end(), FriendId),
        FriendsList.end());
    FriendLevels.erase(FriendId);
}

bool MReflectionSmokePlayerData::HasFriend(uint64 FriendId) const
{
    return std::find(FriendsList.begin(), FriendsList.end(), FriendId) != FriendsList.end();
}
