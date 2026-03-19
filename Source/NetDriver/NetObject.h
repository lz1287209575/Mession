#pragma once

#include "Common/MLib.h"
#include "Common/Logger.h"
#include "Common/StringUtils.h"

// 网络对象基类
class MObject
{
protected:
    uint64 ObjectId;
    bool bReplicated = false;
    
public:
    MObject()
    {
        ObjectId = MUniqueIdGenerator::Generate();
    }
    
    virtual ~MObject() = default;
    
    // 获取对象ID
    uint64 GetObjectId() const { return ObjectId; }
    void SetObjectId(uint64 Id) { ObjectId = Id; }
    
    // 设置是否启用复制
    void SetReplicated(bool b) { bReplicated = b; }
    bool IsReplicated() const { return bReplicated; }
    
    virtual MString ToString() const
    {
        return "MObject{ObjectId=" + MString::ToString(ObjectId) +
               ", Replicated=" + MString(bReplicated ? "true" : "false") + "}";
    }
};

// Actor类 - 可在网络中复制的对象
class MActor : public MObject
{
private:
    bool bActorReplicates = false;
    bool bActorIsActive = false;
    uint64 OwnerId = 0;
    uint32 NetPriority = 1;
    float NetUpdateFrequency = 10.0f; // 每秒更新次数
    float LastNetUpdateTime = 0.0f;
    
protected:
    SVector Location;
    SRotator Rotation;
    SVector Scale = SVector(1.0f, 1.0f, 1.0f);
    
public:
    MActor() = default;
    
    virtual ~MActor() = default;

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
    void SetLocation(const SVector& Loc) 
    { 
        Location = Loc;
    }
    const SVector& GetLocation() const { return Location; }
    
    // 旋转操作
    void SetRotation(const SRotator& Rot)
    {
        Rotation = Rot;
    }
    const SRotator& GetRotation() const { return Rotation; }
    
    // 缩放操作
    void SetScale(const SVector& InScale)
    {
        Scale = InScale;
    }
    const SVector& GetScale() const { return Scale; }
    
    // Tick - 子类可重写
    virtual void Tick(float DeltaTime)
    {
        LastNetUpdateTime += DeltaTime;
    }

    bool IsReadyForNetUpdate() const
    {
        return bActorReplicates && 
               bActorIsActive && 
               NetUpdateFrequency > 0.0f &&
               LastNetUpdateTime >= (1.0f / NetUpdateFrequency);
    }

    void MarkNetUpdateSent()
    {
        LastNetUpdateTime = 0.0f;
    }
};
