#pragma once

#include "NetDriver/Reflection.h"
#include "Core/Net/NetCore.h"

MENUM()
enum class EReflectionSmokeArchetype : uint8
{
    Warrior = 1,
    Mage = 2,
    Rogue = 3
};

MCLASS()
class MReflectionSmokeCharacter : public MReflectObject
{
public:
    MGENERATED_BODY(MReflectionSmokeCharacter, MReflectObject, 0)

    MPROPERTY(Edit | SaveGame)
    FString Name = "Player";

    MPROPERTY(Edit)
    int32 Level = 1;

    MPROPERTY(Edit)
    int64 Experience = 0;

    MPROPERTY(RepNotify)
    SVector Location = SVector::Zero();

    MPROPERTY(RepNotify)
    SRotator Rotation = SRotator();

    MPROPERTY(RepNotify)
    float Health = 100.0f;

    MPROPERTY(Edit)
    float MaxHealth = 100.0f;

    MPROPERTY(Edit)
    float MoveSpeed = 300.0f;

    MPROPERTY(RepNotify)
    bool bIsAlive = true;

    MPROPERTY(Edit | SaveGame)
    int32 Gold = 0;

    MPROPERTY(Edit)
    EReflectionSmokeArchetype Archetype = EReflectionSmokeArchetype::Warrior;

    MFUNCTION()
    void TakeDamage(float Damage);

    MFUNCTION()
    void Heal(float Amount);

    MFUNCTION()
    void MoveTo(const SVector& NewLocation);

    MFUNCTION()
    void LevelUp();

    MFUNCTION()
    void Rename(const FString& NewName);

    MFUNCTION()
    int32 GetGoldAmount() const;

    MFUNCTION()
    bool IsAlive() const;

    virtual void Tick(float DeltaTime) override;
    virtual void BeginPlay() override;
};

MSTRUCT()
struct SReflectionSmokeAttributeSet
{
    MPROPERTY(Edit)
    int32 Strength = 0;

    MPROPERTY(Edit)
    int32 Agility = 0;

    MPROPERTY(Edit)
    int32 Intelligence = 0;
};

MSTRUCT()
struct SReflectionSmokeCombatStats
{
    MPROPERTY(Edit)
    SReflectionSmokeAttributeSet Base;

    MPROPERTY(Edit)
    SReflectionSmokeAttributeSet Bonus;

    MPROPERTY(Edit)
    float CritChance = 0.0f;

    MPROPERTY(Edit)
    float CritMultiplier = 1.0f;
};

MCLASS()
class MReflectionSmokeHero : public MReflectObject
{
public:
    MGENERATED_BODY(MReflectionSmokeHero, MReflectObject, 0)

    MPROPERTY(Edit)
    FString Name = "Hero";

    MPROPERTY(Edit)
    SReflectionSmokeCombatStats CombatStats;

    MPROPERTY(Edit)
    int32 Level = 1;

    MPROPERTY(Edit)
    float Health = 100.0f;

public:
    int32 GetLevel() const { return Level; }
    float GetHealth() const { return Health; }

    MFUNCTION(NetServer)
    void ServerRpc_AddStats(int32 LevelDelta, float HealthDelta);
    bool ServerRpc_AddStats_Validate(int32 LevelDelta, float HealthDelta) const;
};

MCLASS()
class MReflectionSmokePlayerData : public MReflectObject
{
public:
    MGENERATED_BODY(MReflectionSmokePlayerData, MReflectObject, 0)

    using FriendLevelMap = TMap<uint64, int32>;
    using BlackListSet = TSet<uint64>;

    MPROPERTY(Edit | SaveGame)
    uint64 PlayerId = 0;

    MPROPERTY(Edit)
    FString AccountName;

    MPROPERTY(Edit)
    int32 VIPLevel = 0;

    MPROPERTY(SaveGame)
    int64 LoginTime = 0;

    MPROPERTY(SaveGame)
    FString LastLoginIP;

    MPROPERTY(SaveGame)
    TVector<uint64> FriendsList;

    MPROPERTY(SaveGame)
    FriendLevelMap FriendLevels;

    MPROPERTY(SaveGame)
    BlackListSet BlackList;

public:
    MFUNCTION()
    void AddFriend(uint64 FriendId);

    MFUNCTION()
    void RemoveFriend(uint64 FriendId);

    MFUNCTION()
    bool HasFriend(uint64 FriendId) const;

    TVector<uint64>& GetFriendsList() { return FriendsList; }
    FriendLevelMap& GetFriendLevels() { return FriendLevels; }
    BlackListSet& GetBlackList() { return BlackList; }
    const TVector<uint64>& GetFriendsList() const { return FriendsList; }
    const FriendLevelMap& GetFriendLevels() const { return FriendLevels; }
    const BlackListSet& GetBlackList() const { return BlackList; }
};
