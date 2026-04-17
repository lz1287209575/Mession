#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Id.h"
#include "Common/Runtime/StringUtils.h"
#include "Common/Runtime/Reflect/Property.h"
#include "Common/Runtime/Reflect/Class.h"
#include <typeinfo>
#include <typeindex>
#include <type_traits>
#include <stdexcept>
#include <tuple>
// ============================================
// 反射系统核心 - 仿UE风格
// ============================================

class MReflectArchive;
class MObject;
class IClientRouteTarget;
class IClientResponseTarget;
bool BuildServerRpcPayload(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutData);

template<typename T>
using TRpcArgStorage = std::remove_cv_t<std::remove_reference_t<T>>;

template<typename T>
using TReflectStorage = std::remove_cv_t<std::remove_reference_t<T>>;

// ============================================
// 反射对象基类
// ============================================

class MObject
{
private:
    enum : uint64
    {
        ObjectFlag_RootSet = 1ull << 0,
        ObjectFlag_DefaultSubObject = 1ull << 1,
    };

    inline static TMap<MString, MClass*>& GetClassMap()
    {
        static TMap<MString, MClass*> Map;
        return Map;
    }
    
    inline static TMap<uint16, MClass*>& GetClassIdMap()
    {
        static TMap<uint16, MClass*> Map;
        return Map;
    }

    inline static TMap<uint32, MClass*>& GetClassAssetTypeMap()
    {
        static TMap<uint32, MClass*> Map;
        return Map;
    }

    inline static TMap<MString, MEnum*>& GetEnumMap()
    {
        static TMap<MString, MEnum*> Map;
        return Map;
    }

    inline static TMap<uint16, MEnum*>& GetEnumIdMap()
    {
        static TMap<uint16, MEnum*> Map;
        return Map;
    }

    inline static TMap<std::type_index, MEnum*>& GetEnumTypeMap()
    {
        static TMap<std::type_index, MEnum*> Map;
        return Map;
    }

    inline static TMap<MString, MClass*>& GetStructMap()
    {
        static TMap<MString, MClass*> Map;
        return Map;
    }

    inline static TMap<uint16, MClass*>& GetStructIdMap()
    {
        static TMap<uint16, MClass*> Map;
        return Map;
    }

    inline static TMap<uint32, MClass*>& GetStructAssetTypeMap()
    {
        static TMap<uint32, MClass*> Map;
        return Map;
    }

    inline static TMap<std::type_index, MClass*>& GetStructTypeMap()
    {
        static TMap<std::type_index, MClass*> Map;
        return Map;
    }

protected:
    uint64 ObjectId = 0;
    bool bReplicated = false;
    MClass* Class = nullptr;
    uint64 ObjectFlags = 0;
    MString Name;
    MObject* Outer = nullptr;
    TVector<MObject*> Children;
    uint64 DirtyDomainFlags = ToMask(EPropertyDomainFlags::None);
    TSet<uint16> DirtyPropertyIds;
    
public:
    MObject()
        : ObjectId(MUniqueIdGenerator::Generate())
    {
        GetObjectMap()[ObjectId] = this;
    }
    MObject(const MObject&) = delete;
    MObject& operator=(const MObject&) = delete;
    MObject(MObject&&) = delete;
    MObject& operator=(MObject&&) = delete;
    virtual ~MObject();
    
    uint64 GetObjectId() const { return ObjectId; }
    void SetObjectId(uint64 InId)
    {
        if (ObjectId == InId)
        {
            return;
        }

        GetObjectMap().erase(ObjectId);
        ObjectId = InId;
        GetObjectMap()[ObjectId] = this;
    }
    void SetReplicated(bool bInReplicated) { bReplicated = bInReplicated; }
    bool IsReplicated() const { return bReplicated; }

    // 获取类和对象信息
    virtual MClass* GetClass() const { return Class; }
    uint64 GetId() const { return GetObjectId(); }
    const MString& GetName() const { return Name; }
    void SetName(const MString& InName) { Name = InName; }
    MObject* GetOuter() const { return Outer; }
    const TVector<MObject*>& GetChildren() const { return Children; }
    bool IsRooted() const { return (ObjectFlags & ObjectFlag_RootSet) != 0; }
    bool IsDefaultSubObject() const { return (ObjectFlags & ObjectFlag_DefaultSubObject) != 0; }
    
    // 虚函数
    virtual void BeginPlay() {}
    virtual void Tick(float /*DeltaTime*/) {}
    virtual void Destroy() {}
    virtual MString ToString() const;
    virtual IClientRouteTarget* GetClientRouteTarget() { return nullptr; }
    virtual IClientResponseTarget* GetClientResponseTarget() { return nullptr; }
    virtual void VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const;
    
    // 反射方法
    template<typename T>
    T* GetProperty(const MString& InName) const
    {
        if (!Class)
        {
            return nullptr;
        }
        auto* Prop = Class->FindProperty(InName);
        if (!Prop)
        {
            return nullptr;
        }
        return Prop->GetValuePtr<T>(const_cast<void*>(static_cast<const void*>(this)));
    }
    
    template<typename T>
    void SetProperty(const MString& InName, const T& Value)
    {
        if (!Class)
        {
            return;
        }
        auto* Prop = Class->FindProperty(InName);
        if (!Prop)
        {
            return;
        }
        *Prop->GetValuePtr<T>(this) = Value;
        MarkPropertyDirty(Prop);
    }
    
    bool CallFunction(const MString& InName);

    template<typename... TArgs>
    bool CallFunctionArgs(const MString& InName, TArgs&&... Args);

    template<typename TReturn, typename... TArgs>
    bool CallFunctionWithReturn(const MString& InName, TReturn& OutReturn, TArgs&&... Args);

    bool ProcessEvent(const MString& InName, void* Params);
    bool ProcessEvent(MFunction* Func, void* Params);
    bool InvokeSerializedFunction(MFunction* Func, MReflectArchive& InAr);
    
    // 静态方法：类注册
    template<typename T>
    static MClass* StaticClass()
    {
        return T::StaticClass();
    }
    
    static MClass* FindClass(const MString& InName)
    {
        auto It = GetClassMap().find(InName);
        return (It != GetClassMap().end()) ? It->second : nullptr;
    }
    
    static MClass* FindClass(uint16 InId)
    {
        auto It = GetClassIdMap().find(InId);
        return (It != GetClassIdMap().end()) ? It->second : nullptr;
    }

    static MClass* FindClassByAssetTypeId(uint32 InAssetTypeId)
    {
        auto It = GetClassAssetTypeMap().find(InAssetTypeId);
        return (It != GetClassAssetTypeMap().end()) ? It->second : nullptr;
    }

    static TVector<MClass*> GetAllClasses()
    {
        TVector<MClass*> Result;
        Result.reserve(GetClassMap().size());
        for (const auto& Pair : GetClassMap())
        {
            if (Pair.second)
            {
                Result.push_back(Pair.second);
            }
        }
        return Result;
    }
    
    static void RegisterClass(MClass* InClass)
    {
        if (!InClass)
        {
            return;
        }

        if (MClass* ExistingClass = FindClassByAssetTypeId(InClass->GetAssetTypeId());
            ExistingClass && ExistingClass != InClass)
        {
            const MString Message =
                "Reflection asset type id collision: class=" + InClass->GetName() +
                " existing=" + ExistingClass->GetName() +
                " asset_type_id=" + std::to_string(InClass->GetAssetTypeId());
            LOG_FATAL("%s", Message.c_str());
            throw std::runtime_error(Message);
        }

        GetClassMap()[InClass->GetName()] = InClass;
        GetClassIdMap()[InClass->GetId()] = InClass;
        GetClassAssetTypeMap()[InClass->GetAssetTypeId()] = InClass;
    }

    static MObject* FindObject(uint64 InObjectId)
    {
        auto It = GetObjectMap().find(InObjectId);
        return (It != GetObjectMap().end()) ? It->second : nullptr;
    }

    static TVector<MObject*> GetAllObjects()
    {
        TVector<MObject*> Result;
        Result.reserve(GetObjectMap().size());
        for (const auto& Pair : GetObjectMap())
        {
            if (Pair.second)
            {
                Result.push_back(Pair.second);
            }
        }
        return Result;
    }

    static MEnum* FindEnum(const MString& InName)
    {
        auto It = GetEnumMap().find(InName);
        return (It != GetEnumMap().end()) ? It->second : nullptr;
    }

    static MEnum* FindEnum(uint16 InId)
    {
        auto It = GetEnumIdMap().find(InId);
        return (It != GetEnumIdMap().end()) ? It->second : nullptr;
    }

    static MEnum* FindEnum(const std::type_index& InCppTypeIndex)
    {
        auto It = GetEnumTypeMap().find(InCppTypeIndex);
        return (It != GetEnumTypeMap().end()) ? It->second : nullptr;
    }

    static void RegisterEnum(MEnum* InEnum)
    {
        if (!InEnum)
        {
            return;
        }

        GetEnumMap()[InEnum->GetName()] = InEnum;
        GetEnumIdMap()[InEnum->GetId()] = InEnum;
        if (InEnum->GetCppTypeIndex() != std::type_index(typeid(void)))
        {
            GetEnumTypeMap()[InEnum->GetCppTypeIndex()] = InEnum;
        }
    }

    static MClass* FindStruct(const MString& InName)
    {
        auto It = GetStructMap().find(InName);
        return (It != GetStructMap().end()) ? It->second : nullptr;
    }

    static MClass* FindStruct(uint16 InId)
    {
        auto It = GetStructIdMap().find(InId);
        return (It != GetStructIdMap().end()) ? It->second : nullptr;
    }

    static MClass* FindStructByAssetTypeId(uint32 InAssetTypeId)
    {
        auto It = GetStructAssetTypeMap().find(InAssetTypeId);
        return (It != GetStructAssetTypeMap().end()) ? It->second : nullptr;
    }

    static MClass* FindStruct(const std::type_index& InCppTypeIndex)
    {
        auto It = GetStructTypeMap().find(InCppTypeIndex);
        return (It != GetStructTypeMap().end()) ? It->second : nullptr;
    }

    static void RegisterStruct(MClass* InStruct)
    {
        if (!InStruct)
        {
            return;
        }

        if (MClass* ExistingStruct = FindStructByAssetTypeId(InStruct->GetAssetTypeId());
            ExistingStruct && ExistingStruct != InStruct)
        {
            const MString Message =
                "Reflection asset type id collision: struct=" + InStruct->GetName() +
                " existing=" + ExistingStruct->GetName() +
                " asset_type_id=" + std::to_string(InStruct->GetAssetTypeId());
            LOG_FATAL("%s", Message.c_str());
            throw std::runtime_error(Message);
        }

        GetStructMap()[InStruct->GetName()] = InStruct;
        GetStructIdMap()[InStruct->GetId()] = InStruct;
        GetStructAssetTypeMap()[InStruct->GetAssetTypeId()] = InStruct;
        if (InStruct->GetCppTypeIndex() != std::type_index(typeid(void)))
        {
            GetStructTypeMap()[InStruct->GetCppTypeIndex()] = InStruct;
        }
    }
    
    // 供反射系统在创建实例时设置类信息
    void SetClass(MClass* InClass)
    {
        Class = InClass;
    }

    void SetOuter(MObject* InOuter);
    void AddToRoot();
    void RemoveFromRoot();
    void MarkAsDefaultSubObject() { AddObjectFlags(ObjectFlag_DefaultSubObject); }
    void SetObjectFlags(uint64 InFlags) { ObjectFlags = InFlags; }
    void AddObjectFlags(uint64 InFlags) { ObjectFlags |= InFlags; }
    void RemoveObjectFlags(uint64 InFlags) { ObjectFlags &= ~InFlags; }
    uint64 GetObjectFlags() const { return ObjectFlags; }

    void MarkPropertyDirty(const MString& InName)
    {
        if (!Class)
        {
            return;
        }

        MProperty* Prop = Class->FindProperty(InName);
        MarkPropertyDirty(Prop);
    }

    void MarkPropertyDirtyById(uint16 InPropertyId)
    {
        if (!Class)
        {
            return;
        }

        MProperty* Prop = Class->FindPropertyById(InPropertyId);
        MarkPropertyDirty(Prop);
    }

    void SetPropertyDirtyById(uint16 InPropertyId)
    {
        MarkPropertyDirtyById(InPropertyId);
    }

    void MarkPropertyDirty(const MProperty* InProperty)
    {
        if (!InProperty)
        {
            return;
        }

        DirtyPropertyIds.insert(InProperty->PropertyId);
        DirtyDomainFlags |= InProperty->DomainFlags;
    }

    void SetPropertyDirty(const MProperty* InProperty)
    {
        MarkPropertyDirty(InProperty);
    }

    bool HasDirtyDomain(EPropertyDomainFlags InDomain) const
    {
        return HasAnyPropertyDomains(DirtyDomainFlags, InDomain);
    }

    bool HasAnyDirtyPropertyForDomain(EPropertyDomainFlags InDomain) const
    {
        if (!Class || DirtyPropertyIds.empty())
        {
            return false;
        }

        for (uint16 PropertyId : DirtyPropertyIds)
        {
            if (const MProperty* Prop = Class->FindPropertyById(PropertyId))
            {
                if (Prop->HasAnyDomains(InDomain))
                {
                    return true;
                }
            }
        }
        return false;
    }

    void ClearDirtyDomain(EPropertyDomainFlags InDomain)
    {
        if (!Class)
        {
            DirtyDomainFlags &= ~ToMask(InDomain);
            return;
        }

        for (auto It = DirtyPropertyIds.begin(); It != DirtyPropertyIds.end(); )
        {
            const MProperty* Prop = Class->FindPropertyById(*It);
            if (!Prop || Prop->HasAnyDomains(InDomain))
            {
                It = DirtyPropertyIds.erase(It);
            }
            else
            {
                ++It;
            }
        }

        DirtyDomainFlags = ToMask(EPropertyDomainFlags::None);
        for (uint16 PropertyId : DirtyPropertyIds)
        {
            if (const MProperty* Prop = Class->FindPropertyById(PropertyId))
            {
                DirtyDomainFlags |= Prop->DomainFlags;
            }
        }
    }
    
private:
    static TMap<uint64, MObject*>& GetObjectMap();
    static TSet<MObject*>& GetRootSet();
    void AddChildObject(MObject* Child);
    void RemoveChildObject(MObject* Child);

    template<typename TReturn, typename... TArgs>
    bool InvokeFunction(const MString& InName, TReturn* OutReturn, TArgs&&... Args);

    bool InvokeFunction(MFunction* Func, MReflectArchive* InAr, MReflectArchive* OutAr);
};

// ============================================
// 宏定义 - 简化版
// ============================================

// 反射标记宏（标签，不做实际逻辑）
#define MCLASS(...)
#define MSTRUCT(...)
#define MENUM(...)
#define MPROPERTY(...)
#define MFUNCTION(...)

// 注册属性
#define PROPERTY(Type, Name, Flags) \
    struct SProperty_##Name : public MProperty { \
        SProperty_##Name() : MProperty(#Name, EPropertyType::Type, offsetof(ThisClass, Name), sizeof(Type), std::type_index(typeid(Type))) { \
            this->Flags = EPropertyFlags::Flags; \
        } \
    } Name##_Property;

// 注册函数
#define FUNCTION(Name, Flags) \
    struct MFunction_##Name : public MFunction { \
        MFunction_##Name() { \
            Name = #Name; \
            Flags = EFunctionFlags::Flags; \
        } \
    };

// 声明类反射信息：放在类内部
#define MGENERATED_BODY(ClassName, ParentClass, Flags) \
public: \
    using ThisClass = ClassName; \
    using Super = ParentClass; \
    static MClass* StaticClass(); \
    virtual MClass* GetClass() const override { return StaticClass(); } \
private: \
    static void RegisterAllProperties(MClass* InClass); \
    static void RegisterAllFunctions(MClass* InClass);

// 在类外实现反射注册
#define MIMPLEMENT_CLASS(ClassName, ParentClass, Flags) \
MClass* ClassName::StaticClass() \
{ \
    static MClass* Class = nullptr; \
    if (!Class) \
    { \
        Class = new MClass(); \
        Class->SetMeta(#ClassName, __FILE__, nullptr, Flags); \
        Class->SetConstructor<ClassName>(); \
        ClassName::RegisterAllProperties(Class); \
        ClassName::RegisterAllFunctions(Class); \
        MObject::RegisterClass(Class); \
    } \
    return Class; \
}

// 注册属性宏：CppType 为底层 C++ 类型，PropEnum 为 EPropertyType 枚举值
#define MREGISTER_PROPERTY(CppType, PropEnum, PropName, PropFlags) \
    do { \
        auto* Prop = new TMemberProperty<ThisClass, CppType, &ThisClass::PropName>(#PropName, EPropertyType::PropEnum, PropFlags); \
        InClass->RegisterProperty(Prop); \
    } while(0)

// 向反射系统注册 TVector 容器属性（元素类型必须已被 MReflectArchive 支持）

// 注册函数宏
#define MREGISTER_FUNCTION(FuncName, FuncFlags) \
    do { \
        auto* Func = new MFunction(); \
        Func->Name = #FuncName; \
        Func->Flags = EFunctionFlags::FuncFlags; \
        InClass->RegisterFunction(Func); \
    } while(0)

#define MREGISTER_NATIVE_METHOD_0(MethodName, FuncFlags) \
    MREGISTER_NATIVE_METHOD(MethodName, FuncFlags)

#define MREGISTER_NATIVE_METHOD(MethodName, FuncFlags) \
    do { \
        InClass->RegisterFunction(CreateNativeFunction<&ThisClass::MethodName>(#MethodName, FuncFlags)); \
    } while(0)

#define MREGISTER_RPC_FUNCTION(MethodPtr, FuncNameLiteral, FuncFlags, RpcKind, ReliableValue) \
    do { \
        InClass->RegisterFunction( \
            CreateRpcFunction<MethodPtr>(FuncNameLiteral, EFunctionFlags::FuncFlags, ERpcType::RpcKind, ReliableValue)); \
    } while(0)

#define MREGISTER_RPC_FUNCTION_WITH_VALIDATE(MethodPtr, ValidatePtr, FuncNameLiteral, FuncFlags, RpcKind, ReliableValue) \
    do { \
        InClass->RegisterFunction( \
            CreateRpcFunction<MethodPtr, ValidatePtr>(FuncNameLiteral, EFunctionFlags::FuncFlags, ERpcType::RpcKind, ReliableValue)); \
    } while(0)

#define MREGISTER_RPC_FUNCTION_FOR_SERVER(MethodPtr, FuncNameLiteral, FuncFlags, RpcKind, ReliableValue, EndpointType) \
    do { \
        InClass->RegisterFunction( \
            CreateRpcFunction<MethodPtr>( \
                FuncNameLiteral, \
                EFunctionFlags::FuncFlags, \
                ERpcType::RpcKind, \
                ReliableValue, \
                EServerType::EndpointType)); \
    } while(0)

#define MREGISTER_RPC_METHOD(MethodName, FuncFlags, RpcKind, ReliableValue) \
    MREGISTER_RPC_FUNCTION(&ThisClass::MethodName, #MethodName, FuncFlags, RpcKind, ReliableValue)

#define MREGISTER_RPC_METHOD_FOR_SERVER(MethodName, FuncFlags, RpcKind, ReliableValue, EndpointType) \
    MREGISTER_RPC_FUNCTION_FOR_SERVER(&ThisClass::MethodName, #MethodName, FuncFlags, RpcKind, ReliableValue, EndpointType)

#define MREGISTER_RPC_METHOD_WITH_VALIDATE(MethodName, ValidateName, FuncFlags, RpcKind, ReliableValue) \
    MREGISTER_RPC_FUNCTION_WITH_VALIDATE( \
        &ThisClass::MethodName, \
        &ThisClass::ValidateName, \
        #MethodName, \
        FuncFlags, \
        RpcKind, \
        ReliableValue)

#define MDECLARE_RPC_METHOD(MethodName, Signature, FuncFlags, RpcKind, ReliableValue) \
    MFUNCTION(FuncFlags) void MethodName Signature;

#define MREGISTER_RPC_METHOD_ENTRY(MethodName, Signature, FuncFlags, RpcKind, ReliableValue) \
    MREGISTER_RPC_METHOD(MethodName, FuncFlags, RpcKind, ReliableValue);

#define MDECLARE_RPC_METHOD_WITH_HANDLER(MethodName, ApiName, Signature, FuncFlags, RpcKind, ReliableValue) \
    MDECLARE_RPC_METHOD(MethodName, Signature, FuncFlags, RpcKind, ReliableValue)

#define MREGISTER_RPC_METHOD_WITH_HANDLER_ENTRY(MethodName, ApiName, Signature, FuncFlags, RpcKind, ReliableValue) \
    MREGISTER_RPC_METHOD_ENTRY(MethodName, Signature, FuncFlags, RpcKind, ReliableValue)

#define MDECLARE_SERVER_HOSTED_RPC_METHOD(ClassNameLiteral, EndpointType, MethodName, Signature, FuncFlags, RpcKind, ReliableValue) \
    MFUNCTION(FuncFlags) void MethodName Signature;

#define MREGISTER_SERVER_HOSTED_RPC_METHOD_ENTRY(ClassNameLiteral, EndpointType, MethodName, Signature, FuncFlags, RpcKind, ReliableValue) \
    MREGISTER_RPC_METHOD_FOR_SERVER(MethodName, FuncFlags, RpcKind, ReliableValue, EndpointType);

// 属性访问器
#define GET_PROPERTY(Object, Type, Name) \
    (Object)->GetProperty<Type>(#Name)

#define SET_PROPERTY(Object, Type, Name, Value) \
    (Object)->SetProperty<Type>(#Name, Value)

#include "Common/Runtime/Reflect/ReflectionArchiveInvoke.inl"
#include "Common/Runtime/Reflect/ReflectionPropertyTemplates.inl"
