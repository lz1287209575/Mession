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
    // UE 风格：在类内部声明反射信息
    GENERATED_BODY(MCharacter, MReflectObject, 0)
    
    // 属性定义
    UPROPERTY(Edit | SaveGame)
    FString Name = "Player";
    
    UPROPERTY(Edit)
    int32 Level = 1;
    
    UPROPERTY(Edit)
    int64 Experience = 0;
    
    UPROPERTY(RepNotify)
    SVector Location = SVector::Zero();
    
    UPROPERTY(RepNotify)
    SRotator Rotation = SRotator();
    
    UPROPERTY(RepNotify)
    float Health = 100.0f;
    
    UPROPERTY(Edit)
    float MaxHealth = 100.0f;
    
    UPROPERTY(Edit)
    float MoveSpeed = 300.0f;
    
    UPROPERTY(RepNotify)
    bool bIsAlive = true;
    
    UPROPERTY(Edit | SaveGame)
    int32 Gold = 0;
    
    // 函数
    UFUNCTION()
    void TakeDamage(float Damage);
    
    UFUNCTION()
    void Heal(float Amount);
    
    UFUNCTION()
    void MoveTo(const SVector& NewLocation);
    
    UFUNCTION()
    void LevelUp();
    
    // 虚函数
    virtual void Tick(float DeltaTime) override;
    virtual void BeginPlay() override;
};

// 玩家数据类
class MPlayerData : public MReflectObject
{
public:
    GENERATED_BODY(MPlayerData, MReflectObject, 0)
    
    UPROPERTY(Edit | SaveGame)
    uint64 PlayerId = 0;
    
    UPROPERTY(Edit)
    FString AccountName;
    
    UPROPERTY(Edit)
    int32 VIPLevel = 0;
    
    UPROPERTY(SaveGame)
    int64 LoginTime = 0;
    
    UPROPERTY(SaveGame)
    FString LastLoginIP;
    
    // 示例：非简单类型（数组）目前通过自定义序列化处理
    TVector<uint64> FriendsList;
    
    void AddFriend(uint64 FriendId);
    void RemoveFriend(uint64 FriendId);
    bool HasFriend(uint64 FriendId) const;
    
private:
    virtual void Serialize(void* Object, class MReflectArchive& Ar) const;
    virtual void Deserialize(void* Object, const TArray& Data) const;
};
