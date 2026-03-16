#pragma once

#include "NetDriver/Reflection.h"
#include "Core/Net/NetCore.h"

MCLASS()
class MCharacter : public MReflectObject
{
public:
    MGENERATED_BODY(MCharacter, MReflectObject, 0)

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

    MFUNCTION()
    void TakeDamage(float Damage);

    MFUNCTION()
    void Heal(float Amount);

    MFUNCTION()
    void MoveTo(const SVector& NewLocation);

    MFUNCTION()
    void LevelUp();

    virtual void Tick(float DeltaTime) override;
    virtual void BeginPlay() override;
};

struct SAttributeSet
{
    int32 Strength = 0;
    int32 Agility = 0;
    int32 Intelligence = 0;
};

struct SCombatStats
{
    SAttributeSet Base;
    SAttributeSet Bonus;
    float CritChance = 0.0f;
    float CritMultiplier = 1.0f;
};

MCLASS()
class MHero : public MReflectObject
{
public:
    MGENERATED_BODY(MHero, MReflectObject, 0)

    MPROPERTY(Edit)
    FString Name = "Hero";

    MPROPERTY(Edit)
    SCombatStats CombatStats;

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
class MPlayerData : public MReflectObject
{
public:
    MGENERATED_BODY(MPlayerData, MReflectObject, 0)

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

    TVector<uint64> FriendsList;
    FriendLevelMap FriendLevels;
    BlackListSet BlackList;

public:
    TVector<uint64>& GetFriendsList() { return FriendsList; }
    FriendLevelMap& GetFriendLevels() { return FriendLevels; }
    BlackListSet& GetBlackList() { return BlackList; }
    const TVector<uint64>& GetFriendsList() const { return FriendsList; }
    const FriendLevelMap& GetFriendLevels() const { return FriendLevels; }
    const BlackListSet& GetBlackList() const { return BlackList; }

    void AddFriend(uint64 FriendId);
    void RemoveFriend(uint64 FriendId);
    bool HasFriend(uint64 FriendId) const;
};
