#pragma once

#include "Reflection.h"
#include "Core/Net/NetCore.h"

// ============================================
// 使用反射系统的示例类
// ============================================

// 玩家角色类
class MCharacter : public MReflectObject
{
public:
    MGENERATED_BODY(MCharacter, MReflectObject, 0)
    
    // 属性定义
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
    
    // 函数
    MFUNCTION()
    void TakeDamage(float Damage);
    
    MFUNCTION()
    void Heal(float Amount);
    
    MFUNCTION()
    void MoveTo(const SVector& NewLocation);
    
    MFUNCTION()
    void LevelUp();
    
    // 虚函数
    virtual void Tick(float DeltaTime) override;
    virtual void BeginPlay() override;
};

// 嵌套属性用的简单结构体（仅包含 POD 字段，适合按字节序列化）
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

// 带有嵌套结构体属性的示例类
class MHero : public MReflectObject
{
public:
    MGENERATED_BODY(MHero, MReflectObject, 0)

    MPROPERTY(Edit)
    FString Name = "Hero";

    // 复杂嵌套类型：结构体里再嵌套结构体 + 标量
    MPROPERTY(Edit)
    SCombatStats CombatStats;

    // 生命值等基础字段，验证与嵌套结构一起序列化
    MPROPERTY(Edit)
    int32 Level = 1;

    MPROPERTY(Edit)
    float Health = 100.0f;

public:
    int32 GetLevel() const { return Level; }
    float GetHealth() const { return Health; }

    // 示例：带参数的服务器 RPC
    MFUNCTION(NetServer)
    void ServerRpc_AddStats(int32 LevelDelta, float HealthDelta);
    bool ServerRpc_AddStats_Validate(int32 LevelDelta, float HealthDelta) const;
};

// 玩家数据类
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
    
    // 示例：容器类型，通过反射注册为属性
    TVector<uint64> FriendsList;

    // 好友等级（容器：TMap）
    FriendLevelMap FriendLevels;

    // 黑名单（容器：TSet）
    BlackListSet BlackList;

public:
    // 仅用于测试代码访问容器数据
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
