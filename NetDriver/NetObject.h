#pragma once

#include "../Core/NetCore.h"
#include "Replicate.h"
#include "../Common/Logger.h"

// 网络对象基类
class UObject
{
private:
    static uint16 GlobalPropertyId;

protected:
    uint64 ObjectId;
    std::vector<FPropertyDescriptor> RepProperties;
    bool bReplicated = false;
    
public:
    UObject()
    {
        ObjectId = FUniqueIdGenerator::Generate();
    }
    
    virtual ~UObject() = default;
    
    // 获取对象ID
    uint64 GetObjectId() const { return ObjectId; }
    void SetObjectId(uint64 Id) { ObjectId = Id; }
    
    // 设置是否启用复制
    void SetReplicated(bool b) { bReplicated = b; }
    bool IsReplicated() const { return bReplicated; }
    
    // 注册复制属性
    void RegisterPropertyInternal(const char* Name, uint16 Offset, uint16 Size, ELifetimeRep Condition)
    {
        FPropertyDescriptor Desc;
        Desc.PropertyId = ++GlobalPropertyId;
        Desc.Offset = Offset;
        Desc.Size = Size;
        Desc.Condition = Condition;
        
        RepProperties.push_back(Desc);
        
        LOG_DEBUG("Registered property %s (id=%d, offset=%d, size=%d)", 
                  Name, Desc.PropertyId, Desc.Offset, Desc.Size);
    }
    
    // 获取所有属性描述符
    const std::vector<FPropertyDescriptor>& GetRepProperties() const { return RepProperties; }
    
    // 序列化 - 子类重写
    virtual void Serialize(FArchive& Ar) {}
    
    // 复制前检查
    virtual bool HasUnreplicatedChanges() const { return false; }
};

// Actor类 - 可在网络中复制的对象
class AActor : public UObject
{
private:
    bool bActorReplicates = false;
    bool bActorIsActive = false;
    uint64 OwnerId = 0;
    uint32 NetPriority = 1;
    float NetUpdateFrequency = 10.0f; // 每秒更新次数
    float LastNetUpdateTime = 0.0f;
    
protected:
    FVector Location;
    FRotator Rotation;
    FVector Scale = FVector(1.0f, 1.0f, 1.0f);
    
    // 属性变化标记
    bool bLocationDirty = false;
    bool bRotationDirty = false;
    bool bScaleDirty = false;
    
public:
    AActor()
    {
        // 注册基础属性
        RegisterPropertyInternal("Location", offsetof(AActor, Location), sizeof(FVector), ELifetimeRep::Always);
        RegisterPropertyInternal("Rotation", offsetof(AActor, Rotation), sizeof(FRotator), ELifetimeRep::Always);
        RegisterPropertyInternal("Scale", offsetof(AActor, Scale), sizeof(FVector), ELifetimeRep::InitialOnly);
    }
    
    virtual ~AActor() = default;
    
    // 设置复制启用
    void SetActorReplicates(bool b) { bActorReplicates = b; }
    bool DoesActorReplicate() const { return bActorReplicates; }
    
    // 设置激活状态
    void SetActorActive(bool b) { bActorIsActive = b; }
    bool IsActorActive() const { return bActorIsActive; }
    
    // 设置拥有者
    void SetOwnerId(uint64 Id) { OwnerId = Id; }
    uint64 GetOwnerId() const { return OwnerId; }
    
    // 设置网络优先级
    void SetNetPriority(uint32 Priority) { NetPriority = Priority; }
    uint32 GetNetPriority() const { return NetPriority; }
    
    // 设置更新频率
    void SetNetUpdateFrequency(float Freq) { NetUpdateFrequency = Freq; }
    float GetNetUpdateFrequency() const { return NetUpdateFrequency; }
    
    // 位置操作
    void SetLocation(const FVector& Loc) 
    { 
        if (Location.X != Loc.X || Location.Y != Loc.Y || Location.Z != Loc.Z)
        {
            Location = Loc;
            bLocationDirty = true;
        }
    }
    const FVector& GetLocation() const { return Location; }
    
    // 旋转操作
    void SetRotation(const FRotator& Rot)
    {
        if (Rotation.Pitch != Rot.Pitch || Rotation.Yaw != Rot.Yaw || Rotation.Roll != Rot.Roll)
        {
            Rotation = Rot;
            bRotationDirty = true;
        }
    }
    const FRotator& GetRotation() const { return Rotation; }
    
    // 缩放操作
    void SetScale(const FVector& InScale)
    {
        if (Scale.X != InScale.X || Scale.Y != InScale.Y || Scale.Z != InScale.Z)
        {
            Scale = InScale;
            bScaleDirty = true;
        }
    }
    const FVector& GetScale() const { return Scale; }
    
    // 检查是否有未复制的变化
    bool HasUnreplicatedChanges() const override
    {
        return bLocationDirty || bRotationDirty || bScaleDirty;
    }
    
    // 清除脏标记
    void ClearDirtyFlags()
    {
        bLocationDirty = false;
        bRotationDirty = false;
        bScaleDirty = false;
    }
    
    // 获取需要复制的属性数据
    virtual void GetReplicatedProperties(FArchive& Ar) const
    {
        // 序列化所有标记为需要复制的属性
        if (bLocationDirty)
            Ar << const_cast<FVector&>(Location);
        
        if (bRotationDirty)
            Ar << const_cast<FRotator&>(Rotation);
        
        if (bScaleDirty)
            Ar << const_cast<FVector&>(Scale);
    }
    
    // 反序列化属性数据
    virtual void OnRep_Property(const FPropertyDescriptor& Desc, FArchive& Ar)
    {
        // 子类重写处理特定属性变化
        LOG_DEBUG("Actor %llu property %d updated", (unsigned long long)GetObjectId(), Desc.PropertyId);
    }
    
    // Tick - 子类可重写
    virtual void Tick(float DeltaTime)
    {
        LastNetUpdateTime += DeltaTime;
    }
    
    // 是否需要网络更新
    bool NeedsNetUpdate() const
    {
        return bActorReplicates && 
               bActorIsActive && 
               LastNetUpdateTime >= (1.0f / NetUpdateFrequency) &&
               HasUnreplicatedChanges();
    }
};
