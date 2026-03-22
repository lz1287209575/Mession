#pragma once

#include "Common/Runtime/Reflect/Property.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Net/ServerConnection.h"
#include <typeindex>

class MObject;
class MReflectArchive;

inline uint16 ComputeStableReflectId(const char* ScopeName, const char* MemberName)
{
    constexpr uint32 OffsetBasis = 2166136261u;
    constexpr uint32 Prime = 16777619u;

    uint32 Hash = OffsetBasis;
    auto MixString = [&Hash](const char* Text)
    {
        if (!Text)
        {
            return;
        }

        while (*Text)
        {
            Hash ^= static_cast<uint8>(*Text);
            Hash *= Prime;
            ++Text;
        }
    };

    MixString(ScopeName);
    Hash ^= static_cast<uint8>(':');
    Hash *= Prime;
    Hash ^= static_cast<uint8>(':');
    Hash *= Prime;
    MixString(MemberName);

    uint16 Folded = static_cast<uint16>((Hash >> 16) ^ (Hash & 0xFFFFu));
    return (Folded == 0) ? 1u : Folded;
}

// 函数标志
enum class EFunctionFlags : uint32
{
    None = 0,
    Final = 1 << 0,
    RequiredAPI = 1 << 1,
    BlueprintCallable = 1 << 2,
    BlueprintPure = 1 << 3,
    EditorOnly = 1 << 4,
    Const = 1 << 5,
    NetServer = 1 << 6,
    NetClient = 1 << 7,
    NetReliable = 1 << 8,
    WithValidation = 1 << 9,
};

// RPC 类型
enum class ERpcType : uint8
{
    None = 0,
    Server = 1,
    Client = 2,
    Multicast = 3,
    ServerToServer = 4
};

class MEnumValue
{
public:
    MString Name;
    int64 Value = 0;
};

class MEnum
{
private:
    MString EnumName;
    MString EnumPath;
    uint16 EnumId = 0;
    std::type_index CppTypeIndex = typeid(void);
    TVector<MEnumValue> Values;

public:
    MEnum() = default;
    MEnum(const MString& InName, const MString& InPath, const std::type_index& InCppTypeIndex = typeid(void))
        : EnumName(InName)
        , EnumPath(InPath)
        , EnumId(ComputeStableReflectId("MEnum", InName.c_str()))
        , CppTypeIndex(InCppTypeIndex)
    {
    }

    const MString& GetName() const { return EnumName; }
    const MString& GetPath() const { return EnumPath; }
    uint16 GetId() const { return EnumId; }
    const std::type_index& GetCppTypeIndex() const { return CppTypeIndex; }
    const TVector<MEnumValue>& GetValues() const { return Values; }

    void AddValue(const MString& InName, int64 InValue)
    {
        Values.push_back(MEnumValue{InName, InValue});
    }

    const MEnumValue* FindValue(const MString& InName) const
    {
        for (const MEnumValue& Value : Values)
        {
            if (Value.Name == InName)
            {
                return &Value;
            }
        }
        return nullptr;
    }

    const MEnumValue* FindValueByIntegral(int64 InValue) const
    {
        for (const MEnumValue& Value : Values)
        {
            if (Value.Value == InValue)
            {
                return &Value;
            }
        }
        return nullptr;
    }
};

enum class EGeneratedClientCallHandlerResult : uint8
{
    Failed = 0,
    Responded = 1,
    Deferred = 2,
};

class MFunction
{
public:
    using FClientParamBinder = bool(*)(uint64 ConnectionId, const TByteArray& Payload, TByteArray& OutParamStorage);
    using FClientCallHandler = EGeneratedClientCallHandlerResult(*)(MObject* Object, uint64 ConnectionId, const TByteArray& Payload, TByteArray& OutResponsePayload);
    using FServerCallHandler = bool(*)(MObject* Object, const TByteArray& Payload);

    MString Name;
    EFunctionFlags Flags = EFunctionFlags::None;
    uint16 FunctionId = 0;
    size_t ParamSize = 0;
    EServerType EndpointServerType = EServerType::Unknown;
    MString Transport;
    MString MessageName;
    MString RouteName;
    MString TargetName;
    MString AuthMode;
    MString WrapMode;
    FClientParamBinder ClientParamBinder = nullptr;
    FClientCallHandler ClientCallHandler = nullptr;
    FServerCallHandler ServerCallHandler = nullptr;

    using FunctionPtr = void(*)(void*);
    FunctionPtr NativeFunc = nullptr;
    using NativeInvoker = bool(*)(MObject*, MReflectArchive*, MReflectArchive*);
    NativeInvoker NativeInvoke = nullptr;

    ERpcType RpcType = ERpcType::None;
    bool bReliable = true;

    TVector<MProperty*> Params;
    MProperty* ReturnProperty = nullptr;

    MFunction() = default;
    virtual ~MFunction()
    {
        for (MProperty* Param : Params)
        {
            delete Param;
        }
        delete ReturnProperty;
    }
};

class MClass
{
private:
    inline static uint16 GlobalClassId = 0;
    inline static uint16 GlobalPropertyId = 0;
    inline static uint16 GlobalFunctionId = 0;

protected:
    MString ClassName;
    MString ClassPath;
    uint16 ClassId = 0;
    std::type_index CppTypeIndex = typeid(void);

    TVector<MProperty*> Properties;
    TVector<MFunction*> Functions;

    MClass* ParentClass = nullptr;

    using ClassConstructor = void*(*)(void*);
    ClassConstructor Constructor = nullptr;

    uint32 ClassFlags = 0;

public:
    MClass();
    virtual ~MClass();

    const MString& GetName() const { return ClassName; }
    const MString& GetPath() const { return ClassPath; }
    uint16 GetId() const { return ClassId; }
    const std::type_index& GetCppTypeIndex() const { return CppTypeIndex; }
    const MClass* GetParentClass() const { return ParentClass; }

    const TVector<MProperty*>& GetProperties() const { return Properties; }
    MProperty* FindProperty(const MString& InName) const;
    MProperty* FindPropertyById(uint16 InId) const;

    const TVector<MFunction*>& GetFunctions() const { return Functions; }
    MFunction* FindFunction(const MString& InName) const;
    MFunction* FindFunctionById(uint16 InId) const;

    void* CreateInstance() const;
    void Construct(void* Object) const;

    virtual void WriteSnapshot(void* Object, class MReflectArchive& Ar) const;
    virtual void WriteSnapshotByDomain(void* Object, class MReflectArchive& Ar, uint64 InDomainMask) const;
    virtual void ReadSnapshot(void* Object, const TByteArray& Data) const;
    virtual void ReadSnapshotByDomain(void* Object, const TByteArray& Data, uint64 InDomainMask) const;
    MString ExportObjectToString(const void* Object) const;

    void CopyProperties(void* Dest, const void* Src) const;

    bool HasFlags(uint32 InFlags) const { return (ClassFlags & InFlags) != 0; }

    template<typename TObject>
    void SetConstructor()
    {
        Constructor = [](void* InPlace) -> void*
        {
            if (InPlace)
            {
                return new (InPlace) TObject();
            }
            return new TObject();
        };
    }

    void SetMeta(const MString& InName, const MString& InPath, MClass* InParent, uint32 InFlags)
    {
        ClassName = InName;
        ClassPath = InPath;
        ParentClass = InParent;
        ClassFlags = InFlags;
    }

    void SetCppTypeIndex(const std::type_index& InCppTypeIndex)
    {
        CppTypeIndex = InCppTypeIndex;
    }

    void RegisterProperty(MProperty* InProperty)
    {
        InProperty->PropertyId = ++GlobalPropertyId;

        uint64 InferredDomains = ToMask(EPropertyDomainFlags::None);
        if (InProperty->HasAnyFlags(EPropertyFlags::Replicated) ||
            InProperty->HasAnyFlags(EPropertyFlags::RepNotify) ||
            InProperty->HasAnyFlags(EPropertyFlags::RepToClient))
        {
            InferredDomains |= ToMask(EPropertyDomainFlags::Replication);
        }
        if (InProperty->HasAnyFlags(EPropertyFlags::SaveGame) ||
            InProperty->HasAnyFlags(EPropertyFlags::PersistentData))
        {
            InferredDomains |= ToMask(EPropertyDomainFlags::Persistence);
        }
        InProperty->DomainFlags = InferredDomains;
        Properties.push_back(InProperty);
    }

    void RegisterFunction(MFunction* InFunction)
    {
        const uint16 StableId = ComputeStableReflectId(ClassName.c_str(), InFunction ? InFunction->Name.c_str() : "");
        if (InFunction)
        {
            if (FindFunctionById(StableId))
            {
                LOG_WARN("Reflection function id collision: class=%s function=%s id=%u",
                         ClassName.c_str(),
                         InFunction->Name.c_str(),
                         static_cast<unsigned>(StableId));
            }
            InFunction->FunctionId = StableId;
        }
        Functions.push_back(InFunction);
    }
};
