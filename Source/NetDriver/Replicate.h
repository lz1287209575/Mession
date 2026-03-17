#pragma once

#include "Core/Net/NetCore.h"

// 属性复制条件
enum class ELifetimeRep : uint8
{
    None = 0,
    Always = 1,         // 总是复制
    OwnerOnly = 2,      // 仅拥有者
    SkipOwner = 3,      // 跳过拥有者
    InitialOnly = 4,    // 仅初始化
    PerWeapon = 5,      // 每把武器
    Replicated = 6      // 自定义条件
};

// 属性变化回调
using FRepChangedFunc = TFunction<void()>;

// 属性描述符
struct SPropertyDescriptor
{
    uint16 PropertyId;
    uint16 Offset;
    uint16 Size;
    ELifetimeRep Condition;
    FRepChangedFunc ChangedCallback;
    
    SPropertyDescriptor() 
        : PropertyId(0), Offset(0), Size(0), Condition(ELifetimeRep::Always) {}
    
    SPropertyDescriptor(uint16 InId, uint16 InOffset, uint16 InSize, ELifetimeRep InCond)
        : PropertyId(InId), Offset(InOffset), Size(InSize), Condition(InCond) {}
};

// 属性宏 - 用于注册复制属性
#define REGISTER_REP_PROPERTY(ClassName, PropertyName, Condition) \
    RegisterPropertyInternal(#PropertyName, \
        reinterpret_cast<uint16>(offsetof(ClassName, PropertyName)), \
        sizeof(decltype(PropertyName)), \
        Condition)

// 序列化接口
class MArchive
{
public:
    virtual ~MArchive() = default;
    
    virtual MArchive& operator<<(uint8& Value) = 0;
    virtual MArchive& operator<<(uint16& Value) = 0;
    virtual MArchive& operator<<(uint32& Value) = 0;
    virtual MArchive& operator<<(uint64& Value) = 0;
    virtual MArchive& operator<<(int8& Value) = 0;
    virtual MArchive& operator<<(int16& Value) = 0;
    virtual MArchive& operator<<(int32& Value) = 0;
    virtual MArchive& operator<<(int64& Value) = 0;
    virtual MArchive& operator<<(float& Value) = 0;
    virtual MArchive& operator<<(double& Value) = 0;
    virtual MArchive& operator<<(FString& Value) = 0;
    virtual MArchive& operator<<(SVector& Value) = 0;
    virtual MArchive& operator<<(SRotator& Value) = 0;
    
    virtual bool IsLoading() const = 0;
    virtual bool IsSaving() const = 0;
};

// 内存归档（用于序列化）
class MMemoryArchive : public MArchive
{
private:
    enum class EMode : uint8
    {
        Saving,
        Loading
    };

    TArray Data;
    size_t ReadPos;
    EMode Mode;
    
public:
    MMemoryArchive() : ReadPos(0), Mode(EMode::Saving) {}
    explicit MMemoryArchive(const TArray& InData) : Data(InData), ReadPos(0), Mode(EMode::Loading) {}
    
    // 设置数据（加载时）
    void SetData(const TArray& InData)
    {
        Data = InData;
        ReadPos = 0;
        Mode = EMode::Loading;
    }
    
    // 获取数据（保存时）
    const TArray& GetData() const { return Data; }
    
    void Clear()
    {
        Data.clear();
        ReadPos = 0;
        Mode = EMode::Saving;
    }
    
    // MArchive接口
    MArchive& operator<<(uint8& Value) override
    {
        if (IsLoading() && ReadPos + sizeof(Value) <= Data.size())
        {
            Value = Data[ReadPos++];
        }
        else if (IsSaving())
        {
            Data.push_back(Value);
        }
        return *this;
    }
    
    MArchive& operator<<(uint16& Value) override
    {
        if (IsLoading() && ReadPos + sizeof(Value) <= Data.size())
        {
            Value = *(uint16*)&Data[ReadPos];
            ReadPos += sizeof(Value);
        }
        else if (IsSaving())
        {
            uint16 V = Value;
            Data.insert(Data.end(), (uint8*)&V, (uint8*)&V + sizeof(V));
        }
        return *this;
    }
    
    MArchive& operator<<(uint32& Value) override
    {
        if (IsLoading() && ReadPos + sizeof(Value) <= Data.size())
        {
            Value = *(uint32*)&Data[ReadPos];
            ReadPos += sizeof(Value);
        }
        else if (IsSaving())
        {
            uint32 V = Value;
            Data.insert(Data.end(), (uint8*)&V, (uint8*)&V + sizeof(V));
        }
        return *this;
    }
    
    MArchive& operator<<(uint64& Value) override
    {
        if (IsLoading() && ReadPos + sizeof(Value) <= Data.size())
        {
            Value = *(uint64*)&Data[ReadPos];
            ReadPos += sizeof(Value);
        }
        else if (IsSaving())
        {
            uint64 V = Value;
            Data.insert(Data.end(), (uint8*)&V, (uint8*)&V + sizeof(V));
        }
        return *this;
    }
    
    MArchive& operator<<(int8& Value) override
    {
        if (IsLoading() && ReadPos + sizeof(Value) <= Data.size())
        {
            Value = (int8)Data[ReadPos++];
        }
        else if (IsSaving())
        {
            Data.push_back((uint8)Value);
        }
        return *this;
    }
    
    MArchive& operator<<(int16& Value) override
    {
        if (IsLoading() && ReadPos + sizeof(Value) <= Data.size())
        {
            Value = *(int16*)&Data[ReadPos];
            ReadPos += sizeof(Value);
        }
        else if (IsSaving())
        {
            int16 V = Value;
            Data.insert(Data.end(), (uint8*)&V, (uint8*)&V + sizeof(V));
        }
        return *this;
    }
    
    MArchive& operator<<(int32& Value) override
    {
        if (IsLoading() && ReadPos + sizeof(Value) <= Data.size())
        {
            Value = *(int32*)&Data[ReadPos];
            ReadPos += sizeof(Value);
        }
        else if (IsSaving())
        {
            int32 V = Value;
            Data.insert(Data.end(), (uint8*)&V, (uint8*)&V + sizeof(V));
        }
        return *this;
    }
    
    MArchive& operator<<(int64& Value) override
    {
        if (IsLoading() && ReadPos + sizeof(Value) <= Data.size())
        {
            Value = *(int64*)&Data[ReadPos];
            ReadPos += sizeof(Value);
        }
        else if (IsSaving())
        {
            int64 V = Value;
            Data.insert(Data.end(), (uint8*)&V, (uint8*)&V + sizeof(V));
        }
        return *this;
    }
    
    MArchive& operator<<(float& Value) override
    {
        if (IsLoading() && ReadPos + sizeof(Value) <= Data.size())
        {
            Value = *(float*)&Data[ReadPos];
            ReadPos += sizeof(Value);
        }
        else if (IsSaving())
        {
            float V = Value;
            Data.insert(Data.end(), (uint8*)&V, (uint8*)&V + sizeof(V));
        }
        return *this;
    }
    
    MArchive& operator<<(double& Value) override
    {
        if (IsLoading() && ReadPos + sizeof(Value) <= Data.size())
        {
            Value = *(double*)&Data[ReadPos];
            ReadPos += sizeof(Value);
        }
        else if (IsSaving())
        {
            double V = Value;
            Data.insert(Data.end(), (uint8*)&V, (uint8*)&V + sizeof(V));
        }
        return *this;
    }
    
    MArchive& operator<<(FString& Value) override
    {
        uint32 Len = (uint32)Value.size();
        *this << Len;
        
        if (IsLoading() && ReadPos + Len <= Data.size())
        {
            Value.assign((char*)&Data[ReadPos], Len);
            ReadPos += Len;
        }
        else if (IsSaving())
        {
            Data.insert(Data.end(), Value.begin(), Value.end());
        }
        return *this;
    }
    
    MArchive& operator<<(SVector& Value) override
    {
        *this << Value.X << Value.Y << Value.Z;
        return *this;
    }
    
    MArchive& operator<<(SRotator& Value) override
    {
        *this << Value.Pitch << Value.Yaw << Value.Roll;
        return *this;
    }
    
    bool IsLoading() const override { return Mode == EMode::Loading; }
    bool IsSaving() const override { return Mode == EMode::Saving; }
};
