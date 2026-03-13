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
    using ThisClass = MCharacter;
    
    // 属性定义
    FString Name = "Player";
    int32 Level = 1;
    int64 Experience = 0;
    SVector Location = SVector::Zero();
    SRotator Rotation = SRotator();
    float Health = 100.0f;
    float MaxHealth = 100.0f;
    float MoveSpeed = 300.0f;
    bool bIsAlive = true;
    int32 Gold = 0;
    
    // 声明UCLASS
    UCLASS(MCharacter, MReflectObject, 0)
    
    // 函数
    void TakeDamage(float Damage);
    void Heal(float Amount);
    void MoveTo(const SVector& NewLocation);
    void LevelUp();
    
    // 虚函数
    virtual void Tick(float DeltaTime) override;
    virtual void BeginPlay() override;
};

// 函数实现
inline void MCharacter::TakeDamage(float Damage)
{
    Health -= Damage;
    if (Health <= 0)
    {
        Health = 0;
        bIsAlive = false;
        LOG_INFO("%s died!", Name.c_str());
    }
}

inline void MCharacter::Heal(float Amount)
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

inline void MCharacter::MoveTo(const SVector& NewLocation)
{
    Location = NewLocation;
    LOG_DEBUG("%s moved to (%.2f, %.2f, %.2f)", 
              Name.c_str(), Location.X, Location.Y, Location.Z);
}

inline void MCharacter::LevelUp()
{
    Level++;
    MaxHealth += 20;
    Health = MaxHealth;
    LOG_INFO("%s leveled up! Now level %d", Name.c_str(), Level);
}

inline void MCharacter::Tick(float DeltaTime)
{
    // 可以在这里添加移动逻辑
}

inline void MCharacter::BeginPlay()
{
    LOG_INFO("%s spawned at level %d", Name.c_str(), Level);
}

// 注册属性和函数
inline void MCharacter::RegisterAllProperties(MClass* InClass)
{
    REGISTER_PROPERTY(String, Name, Edit | SaveGame);
    REGISTER_PROPERTY(Int32, Level, Edit);
    REGISTER_PROPERTY(Int64, Experience, Edit);
    REGISTER_PROPERTY(Vector, Location, RepNotify);
    REGISTER_PROPERTY(Rotator, Rotation, RepNotify);
    REGISTER_PROPERTY(Float, Health, RepNotify);
    REGISTER_PROPERTY(Float, MaxHealth, Edit);
    REGISTER_PROPERTY(Float, MoveSpeed, Edit);
    REGISTER_PROPERTY(Bool, bIsAlive, RepNotify);
    REGISTER_PROPERTY(Int32, Gold, Edit | SaveGame);
}

inline void MCharacter::RegisterAllFunctions(MClass* InClass)
{
    // 可以在这里注册RPC函数
}

// 玩家数据类
class MPlayerData : public MReflectObject
{
public:
    using ThisClass = MPlayerData;
    
    uint64 PlayerId = 0;
    FString AccountName;
    int32 VIPLevel = 0;
    int64 LoginTime = 0;
    FString LastLoginIP;
    TVector<uint64> FriendsList;
    
    UCLASS(MPlayerData, MReflectObject, 0)
    
    void AddFriend(uint64 FriendId);
    void RemoveFriend(uint64 FriendId);
    bool HasFriend(uint64 FriendId) const;
    
private:
    virtual void Serialize(void* Object, class MReflectArchive& Ar) const override;
    virtual void Deserialize(void* Object, const TArray& Data) const override;
};

inline void MPlayerData::AddFriend(uint64 FriendId)
{
    if (!HasFriend(FriendId))
    {
        FriendsList.push_back(FriendId);
    }
}

inline void MPlayerData::RemoveFriend(uint64 FriendId)
{
    for (auto it = FriendsList.begin(); it != FriendsList.end(); ++it)
    {
        if (*it == FriendId)
        {
            FriendsList.erase(it);
            break;
        }
    }
}

inline bool MPlayerData::HasFriend(uint64 FriendId) const
{
    for (uint64 id : FriendsList)
    {
        if (id == FriendId)
        {
            return true;
        }
    }
    return false;
}

inline void MPlayerData::Serialize(void* Object, MReflectArchive& Ar) const
{
    auto* Data = static_cast<MPlayerData*>(Object);
    Ar << Data->PlayerId;
    Ar << Data->AccountName;
    Ar << Data->VIPLevel;
    Ar << Data->LoginTime;
    Ar << Data->LastLoginIP;
    // 序列化好友列表
    uint32 FriendCount = (uint32)Data->FriendsList.size();
    Ar << FriendCount;
    for (uint64 fid : Data->FriendsList)
    {
        Ar << fid;
    }
}

inline void MPlayerData::Deserialize(void* Object, const TArray& Data) const
{
    auto* PlayerData = static_cast<MPlayerData*>(Object);
    MReflectArchive Ar(Data);
    Ar << PlayerData->PlayerId;
    Ar << PlayerData->AccountName;
    Ar << PlayerData->VIPLevel;
    Ar << PlayerData->LoginTime;
    Ar << PlayerData->LastLoginIP;
    uint32 FriendCount = 0;
    Ar << FriendCount;
    PlayerData->FriendsList.resize(FriendCount);
    for (uint32 i = 0; i < FriendCount; ++i)
    {
        Ar << PlayerData->FriendsList[i];
    }
}

inline void MPlayerData::RegisterAllProperties(MClass* InClass)
{
    REGISTER_PROPERTY(UInt64, PlayerId, Edit | SaveGame);
    REGISTER_PROPERTY(String, AccountName, Edit);
    REGISTER_PROPERTY(Int32, VIPLevel, Edit);
    REGISTER_PROPERTY(Int64, LoginTime, SaveGame);
    REGISTER_PROPERTY(String, LastLoginIP, SaveGame);
}

inline void MPlayerData::RegisterAllFunctions(MClass* InClass)
{
    // 不需要注册成员函数作为RPC
}
