#pragma once

#include "Core/Net/NetCore.h"
#include "Common/Logger.h"
#include "Common/ServerConnection.h"
#include <typeinfo>
#include <typeindex>
#include <cstring>
#include <type_traits>
#include <tuple>
// ============================================
// 反射系统核心 - 仿UE风格
// ============================================

// 属性类型
enum class EPropertyType : uint8
{
    None = 0,
    Int8 = 1,
    Int16 = 2,
    Int32 = 3,
    Int64 = 4,
    UInt8 = 5,
    UInt16 = 6,
    UInt32 = 7,
    UInt64 = 8,
    Float = 9,
    Double = 10,
    Bool = 11,
    String = 12,
    Name = 13,
    Vector = 14,
    Rotator = 15,
    Struct = 16,
    Object = 17,
    Class = 18,
    Enum = 19,
    Array = 20      // 容器（如 TVector<T>）
};

// 属性标志
enum class EPropertyFlags : uint64
{
    None = 0,
    Edit = 1 << 0,           // 可编辑
    BlueprintVisible = 1 << 1,  // 蓝图可见
    BlueprintCallable = 1 << 2, // 蓝图可调用
    BlueprintPure = 1 << 3,     // 蓝图纯函数
    EditConst = 1 << 4,         // 常量编辑
    Instanced = 1 << 5,         // 实例化
    Export = 1 << 6,            // 导出
    SaveGame = 1 << 7,          // 可存档
    Replicated = 1 << 8,        // 复制
    RepSkip = 1 << 9,          // 跳过复制
    RepNotify = 1 << 10,        // 复制通知
};

// 函数标志
enum class EFunctionFlags : uint32
{
    None = 0,
    Final = 1 << 0,             // 最终
    RequiredAPI = 1 << 1,        // 必需API
    BlueprintCallable = 1 << 2,  // 蓝图可调用
    BlueprintPure = 1 << 3,      // 蓝图纯函数
    EditorOnly = 1 << 4,         // 仅编辑器
    Const = 1 << 5,             // 常量
    NetServer = 1 << 6,         // 服务器执行
    NetClient = 1 << 7,         // 客户端执行
    NetReliable = 1 << 8,       // 可靠
    WithValidation = 1 << 9,   // 带验证
};

// RPC 类型
enum class ERpcType : uint8
{
    None = 0,
    Server = 1,         // 客户端 -> 服务器
    Client = 2,         // 服务器 -> 单个客户端
    Multicast = 3,      // 服务器 -> 多个客户端
    ServerToServer = 4  // 服务器 <-> 服务器
};

class MReflectArchive;
class MReflectObject;

template<typename T>
using TRpcArgStorage = std::remove_cv_t<std::remove_reference_t<T>>;

inline uint16 ComputeStableReflectId(const char* ScopeName, const char* MemberName)
{
    // FNV-1a 32-bit, then fold to 16-bit. This makes IDs reproducible across processes/binaries.
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

// 属性基础类
class MProperty
{
public:
    using MutableAccessor = void*(*)(void*);
    using ConstAccessor = const void*(*)(const void*);

    FString Name;
    EPropertyType Type = EPropertyType::None;
    EPropertyFlags Flags = EPropertyFlags::None;
    size_t Offset = 0;
    size_t Size = 0;
    uint16 PropertyId = 0;
    std::type_index CppTypeIndex = typeid(void);
    MutableAccessor MutableValueAccessor = nullptr;
    ConstAccessor ConstValueAccessor = nullptr;

    MProperty() = default;
    MProperty(const FString& InName, EPropertyType InType, size_t InOffset, size_t InSize)
        : Name(InName)
        , Type(InType)
        , Offset(InOffset)
        , Size(InSize)
        , PropertyId(0)
        , CppTypeIndex(typeid(void))
    {
    }

    MProperty(const FString& InName, EPropertyType InType, size_t InOffset, size_t InSize, const std::type_index& InCppTypeIndex)
        : Name(InName)
        , Type(InType)
        , Flags(EPropertyFlags::None)
        , Offset(InOffset)
        , Size(InSize)
        , PropertyId(0)
        , CppTypeIndex(InCppTypeIndex)
    {
    }

    MProperty(
        const FString& InName,
        EPropertyType InType,
        size_t InOffset,
        size_t InSize,
        const std::type_index& InCppTypeIndex,
        MutableAccessor InMutableAccessor,
        ConstAccessor InConstAccessor)
        : Name(InName)
        , Type(InType)
        , Flags(EPropertyFlags::None)
        , Offset(InOffset)
        , Size(InSize)
        , PropertyId(0)
        , CppTypeIndex(InCppTypeIndex)
        , MutableValueAccessor(InMutableAccessor)
        , ConstValueAccessor(InConstAccessor)
    {
    }

    virtual ~MProperty() = default;

    // 默认基于 Type 做序列化，容器等复杂类型可在子类中重写
    virtual void SerializeValue(void* Object, MReflectArchive& Ar) const;

    // 获取属性值（模板化）
    template<typename T>
    T* GetValuePtr(void* Object) const
    {
        if (!Object)
        {
            return nullptr;
        }
        if (MutableValueAccessor)
        {
            return static_cast<T*>(MutableValueAccessor(Object));
        }
        return reinterpret_cast<T*>(reinterpret_cast<uint8*>(Object) + Offset);
    }

    template<typename T>
    const T* GetValuePtr(const void* Object) const
    {
        if (!Object)
        {
            return nullptr;
        }
        if (ConstValueAccessor)
        {
            return static_cast<const T*>(ConstValueAccessor(Object));
        }
        return reinterpret_cast<const T*>(reinterpret_cast<const uint8*>(Object) + Offset);
    }

    void* GetValueVoidPtr(void* Object) const
    {
        if (!Object)
        {
            return nullptr;
        }
        if (MutableValueAccessor)
        {
            return MutableValueAccessor(Object);
        }
        return reinterpret_cast<uint8*>(Object) + Offset;
    }

    const void* GetValueVoidPtr(const void* Object) const
    {
        if (!Object)
        {
            return nullptr;
        }
        if (ConstValueAccessor)
        {
            return ConstValueAccessor(Object);
        }
        return reinterpret_cast<const uint8*>(Object) + Offset;
    }
};

// 函数定义
class MFunction
{
public:
    FString Name;
    EFunctionFlags Flags = EFunctionFlags::None;
    uint16 FunctionId = 0;
    size_t ParamSize = 0;
    EServerType EndpointServerType = EServerType::Unknown;
    
    // 函数指针类型
    using FunctionPtr = void(*)(void*);
    FunctionPtr NativeFunc = nullptr;

    // RPC 元信息
    ERpcType RpcType = ERpcType::None;
    bool bReliable = true;

    // RPC 执行入口：由网络层传入对象和参数归档
    using RpcInvoker = void(*)(MReflectObject*, MReflectArchive&);
    RpcInvoker RpcFunc = nullptr;
    
    // 参数列表
    TVector<MProperty*> Params;
    MProperty* ReturnProperty = nullptr;
    
    MFunction() = default;
    virtual ~MFunction() = default;
};

// 类元数据
class MClass
{
private:
    inline static uint16 GlobalClassId = 0;
    inline static uint16 GlobalPropertyId = 0;
    inline static uint16 GlobalFunctionId = 0;

protected:
    FString ClassName;
    FString ClassPath;
    uint16 ClassId = 0;
    
    // 属性和函数
    TVector<MProperty*> Properties;
    TVector<MFunction*> Functions;
    
    // 父类
    MClass* ParentClass = nullptr;
    
    // 创建函数
    using ClassConstructor = void*(*)(void*);
    ClassConstructor Constructor = nullptr;
    
    // 类标志
    uint32 ClassFlags = 0;
    
public:
    MClass();
    virtual ~MClass();
    
    // 获取类名
    const FString& GetName() const { return ClassName; }
    const FString& GetPath() const { return ClassPath; }
    uint16 GetId() const { return ClassId; }
    
    // 获取属性
    const TVector<MProperty*>& GetProperties() const { return Properties; }
    MProperty* FindProperty(const FString& InName) const;
    MProperty* FindPropertyById(uint16 InId) const;
    
    // 获取函数列表
    const TVector<MFunction*>& GetFunctions() const { return Functions; }
    MFunction* FindFunction(const FString& InName) const;
    MFunction* FindFunctionById(uint16 InId) const;
    
    // 创建实例
    void* CreateInstance() const;
    void Construct(void* Object) const;
    
    // 序列化
    virtual void Serialize(void* Object, class MReflectArchive& Ar) const;
    virtual void Deserialize(void* Object, const TArray& Data) const;
    
    // 复制属性
    void CopyProperties(void* Dest, const void* Src) const;
    
    // 检查类标志
    bool HasFlags(uint32 InFlags) const { return (ClassFlags & InFlags) != 0; }
    
    // 构造函数注册
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
    
    // 元信息设置接口（供宏使用）
    void SetMeta(const FString& InName, const FString& InPath, MClass* InParent, uint32 InFlags)
    {
        ClassName = InName;
        ClassPath = InPath;
        ParentClass = InParent;
        ClassFlags = InFlags;
    }
    
    // 注册属性
    void RegisterProperty(MProperty* InProperty)
    {
        InProperty->PropertyId = ++GlobalPropertyId;
        Properties.push_back(InProperty);
    }
    
    // 注册函数
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

// ============================================
// 反射对象基类
// ============================================

class MReflectObject
{
private:
    inline static TMap<FString, MClass*>& GetClassMap()
    {
        static TMap<FString, MClass*> Map;
        return Map;
    }
    
    inline static TMap<uint16, MClass*>& GetClassIdMap()
    {
        static TMap<uint16, MClass*> Map;
        return Map;
    }

protected:
    MClass* Class = nullptr;
    uint64 ObjectFlags = 0;
    uint64 ObjectId = 0;
    FString Name;
    
public:
    MReflectObject() : ObjectId(++GlobalObjectId) {}
    virtual ~MReflectObject() = default;
    
    // 获取类和对象信息
    virtual MClass* GetClass() const { return Class; }
    uint64 GetId() const { return ObjectId; }
    const FString& GetName() const { return Name; }
    
    // 虚函数
    virtual void BeginPlay() {}
    virtual void Tick(float DeltaTime) {}
    virtual void Destroy() {}
    
    // 反射方法
    template<typename T>
    T* GetProperty(const FString& InName) const
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
    void SetProperty(const FString& InName, const T& Value)
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
    }
    
    bool CallFunction(const FString& InName);
    
    // 静态方法：类注册
    template<typename T>
    static MClass* StaticClass()
    {
        return T::StaticClass();
    }
    
    static MClass* FindClass(const FString& InName)
    {
        auto It = GetClassMap().find(InName);
        return (It != GetClassMap().end()) ? It->second : nullptr;
    }
    
    static MClass* FindClass(uint16 InId)
    {
        auto It = GetClassIdMap().find(InId);
        return (It != GetClassIdMap().end()) ? It->second : nullptr;
    }
    
    static void RegisterClass(MClass* InClass)
    {
        GetClassMap()[InClass->GetName()] = InClass;
        GetClassIdMap()[InClass->GetId()] = InClass;
    }
    
    // 供反射系统在创建实例时设置类信息
    void SetClass(MClass* InClass)
    {
        Class = InClass;
    }
    
private:
    inline static uint64 GlobalObjectId = 0;
};

// ============================================
// 宏定义 - 简化版
// ============================================

// 反射标记宏（标签，不做实际逻辑）
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

// MCLASS：类反射声明宏
#define MCLASS(ClassName, ParentClass, Flags) \
    MGENERATED_BODY(ClassName, ParentClass, Flags)

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
        MReflectObject::RegisterClass(Class); \
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
    do { \
        auto* Func = new MFunction(); \
        Func->Name = #MethodName; \
        Func->Flags = EFunctionFlags::FuncFlags; \
        Func->NativeFunc = [](void* Obj) \
        { \
            if (!Obj) \
            { \
                return; \
            } \
            static_cast<ThisClass*>(Obj)->MethodName(); \
        }; \
        InClass->RegisterFunction(Func); \
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
    MFUNCTION(FuncFlags, EndpointType) void MethodName Signature;

#define MREGISTER_SERVER_HOSTED_RPC_METHOD_ENTRY(ClassNameLiteral, EndpointType, MethodName, Signature, FuncFlags, RpcKind, ReliableValue) \
    MREGISTER_RPC_METHOD_FOR_SERVER(MethodName, FuncFlags, RpcKind, ReliableValue, EndpointType);

// 属性访问器
#define GET_PROPERTY(Object, Type, Name) \
    (Object)->GetProperty<Type>(#Name)

#define SET_PROPERTY(Object, Type, Name, Value) \
    (Object)->SetProperty<Type>(#Name, Value)

// ============================================
// 序列化系统
// ============================================

class MReflectArchive
{
public:
    TArray Data;
    size_t ReadPos = 0;
    bool bReading = false;
    bool bWriting = true;
    
    MReflectArchive() { bWriting = true; bReading = false; }
    explicit MReflectArchive(const TArray& InData) : Data(InData), ReadPos(0), bReading(true), bWriting(false) {}
    
    // 序列化基本类型
    MReflectArchive& operator<<(uint8& Value) { return SerializePrimitive(Value); }
    MReflectArchive& operator<<(uint16& Value) { return SerializePrimitive(Value); }
    MReflectArchive& operator<<(uint32& Value) { return SerializePrimitive(Value); }
    MReflectArchive& operator<<(uint64& Value) { return SerializePrimitive(Value); }
    MReflectArchive& operator<<(int8& Value) { return SerializePrimitive(Value); }
    MReflectArchive& operator<<(int16& Value) { return SerializePrimitive(Value); }
    MReflectArchive& operator<<(int32& Value) { return SerializePrimitive(Value); }
    MReflectArchive& operator<<(int64& Value) { return SerializePrimitive(Value); }
    MReflectArchive& operator<<(float& Value) { return SerializePrimitive(Value); }
    MReflectArchive& operator<<(double& Value) { return SerializePrimitive(Value); }
    MReflectArchive& operator<<(FString& Value) { return SerializeString(Value); }
    MReflectArchive& operator<<(SVector& Value) { return SerializePrimitive(Value); }
    MReflectArchive& operator<<(SRotator& Value) { return SerializePrimitive(Value); }
    MReflectArchive& operator<<(bool& Value)
    {
        uint8 Temp = Value ? 1u : 0u;
        *this << Temp;
        if (bReading)
        {
            Value = (Temp != 0u);
        }
        return *this;
    }

    // 原始字节序列化，主要用于结构体等复杂类型的按字节拷贝
    MReflectArchive& SerializeBytes(void* Buffer, size_t Size)
    {
        if (!Buffer || Size == 0)
        {
            return *this;
        }

        if (bWriting)
        {
            size_t OldSize = Data.size();
            Data.resize(OldSize + Size);
            memcpy(Data.data() + OldSize, Buffer, Size);
        }
        else if (bReading && ReadPos + Size <= Data.size())
        {
            memcpy(Buffer, Data.data() + ReadPos, Size);
            ReadPos += Size;
        }
        return *this;
    }
    
private:
    template<typename T>
    MReflectArchive& SerializePrimitive(T& Value)
    {
        if (bWriting)
        {
            size_t OldSize = Data.size();
            Data.resize(OldSize + sizeof(T));
            memcpy(Data.data() + OldSize, &Value, sizeof(T));
        }
        else if (bReading && ReadPos + sizeof(T) <= Data.size())
        {
            memcpy(&Value, Data.data() + ReadPos, sizeof(T));
            ReadPos += sizeof(T);
        }
        return *this;
    }
    
    MReflectArchive& SerializeString(FString& Value)
    {
        if (bWriting)
        {
            uint32 Len = (uint32)Value.size();
            *this << Len;
            if (Len > 0)
            {
                size_t OldSize = Data.size();
                Data.resize(OldSize + Len);
                memcpy(Data.data() + OldSize, Value.data(), Len);
            }
        }
        else if (bReading)
        {
            uint32 Len = 0;
            *this << Len;
            if (Len > 0 && ReadPos + Len <= Data.size())
            {
                Value.assign((char*)Data.data() + ReadPos, Len);
                ReadPos += Len;
            }
        }
        return *this;
    }
};

template<typename... TArgs>
inline void SerializeRpcArgs(MReflectArchive& Ar, TArgs&... Args)
{
    (void(Ar << Args), ...);
}

template<typename... TArgs>
inline TArray BuildRpcArgsPayload(TArgs&&... Args)
{
    MReflectArchive Ar;
    (void([&Ar](auto&& Value)
    {
        using TValue = std::remove_cv_t<std::remove_reference_t<decltype(Value)>>;
        TValue Copy = static_cast<TValue>(Value);
        Ar << Copy;
    }(std::forward<TArgs>(Args))), ...);
    return std::move(Ar.Data);
}

template<auto MethodPtr>
struct TRpcMethodTraits;

template<typename TObject, typename... TArgs, void (TObject::*MethodPtr)(TArgs...)>
struct TRpcMethodTraits<MethodPtr>
{
    using ObjectType = TObject;
    using ArgsTuple = std::tuple<TRpcArgStorage<TArgs>...>;
};

template<auto MethodPtr, auto ValidatePtr = nullptr>
struct TRpcInvoker;

template<typename TObject, typename... TArgs, void (TObject::*MethodPtr)(TArgs...), bool (TObject::*ValidatePtr)(TArgs...) const>
struct TRpcInvoker<MethodPtr, ValidatePtr>
{
    using Traits = TRpcMethodTraits<MethodPtr>;
    using ObjectType = typename Traits::ObjectType;
    using ArgsTuple = typename Traits::ArgsTuple;

    template<size_t... Indices>
    static void InvokeImpl(ObjectType* Object, MReflectArchive& Ar, std::index_sequence<Indices...>)
    {
        ArgsTuple Args;
        SerializeRpcArgs(Ar, std::get<Indices>(Args)...);

        const bool bValid = std::apply(
            [Object](auto&... UnpackedArgs)
            {
                return (Object->*ValidatePtr)(UnpackedArgs...);
            },
            Args);
        if (!bValid)
        {
            LOG_WARN("RPC validation failed for function");
            return;
        }

        std::apply(
            [Object](auto&... UnpackedArgs)
            {
                (Object->*MethodPtr)(UnpackedArgs...);
            },
            Args);
    }

    static void Invoke(MReflectObject* Object, MReflectArchive& Ar)
    {
        if (!Object)
        {
            return;
        }

        auto* TypedObject = static_cast<ObjectType*>(Object);
        InvokeImpl(TypedObject, Ar, std::index_sequence_for<TArgs...>{});
    }
};

template<typename TObject, typename... TArgs, void (TObject::*MethodPtr)(TArgs...)>
struct TRpcInvoker<MethodPtr, nullptr>
{
    using Traits = TRpcMethodTraits<MethodPtr>;
    using ObjectType = typename Traits::ObjectType;
    using ArgsTuple = typename Traits::ArgsTuple;

    template<size_t... Indices>
    static void InvokeImpl(ObjectType* Object, MReflectArchive& Ar, std::index_sequence<Indices...>)
    {
        ArgsTuple Args;
        SerializeRpcArgs(Ar, std::get<Indices>(Args)...);

        std::apply(
            [Object](auto&... UnpackedArgs)
            {
                (Object->*MethodPtr)(UnpackedArgs...);
            },
            Args);
    }

    static void Invoke(MReflectObject* Object, MReflectArchive& Ar)
    {
        if (!Object)
        {
            return;
        }

        auto* TypedObject = static_cast<ObjectType*>(Object);
        InvokeImpl(TypedObject, Ar, std::index_sequence_for<TArgs...>{});
    }
};

template<auto MethodPtr, auto ValidatePtr = nullptr>
inline MFunction* CreateRpcFunction(
    const char* Name,
    EFunctionFlags Flags,
    ERpcType RpcType,
    bool bReliable,
    EServerType EndpointServerType = EServerType::Unknown)
{
    auto* Func = new MFunction();
    Func->Name = Name;
    Func->Flags = Flags;
    Func->RpcType = RpcType;
    Func->bReliable = bReliable;
    Func->EndpointServerType = EndpointServerType;
    Func->RpcFunc = &TRpcInvoker<MethodPtr, ValidatePtr>::Invoke;
    return Func;
}

template<auto MethodPtr, typename... TArgs>
inline TArray BuildRpcPayloadForCall(TArgs&&... Args)
{
    using Traits = TRpcMethodTraits<MethodPtr>;
    using ObjectType = typename Traits::ObjectType;
    (void)sizeof(ObjectType);
    return BuildRpcArgsPayload(std::forward<TArgs>(Args)...);
}

template<typename TObject>
inline uint16 GetRpcFunctionIdByName(const char* Name)
{
    MClass* Class = TObject::StaticClass();
    if (!Class || !Name)
    {
        return 0;
    }

    MFunction* Func = Class->FindFunction(Name);
    return Func ? Func->FunctionId : 0;
}

inline uint16 GetStableRpcFunctionIdByName(const char* ClassName, const char* FuncName)
{
    return ComputeStableReflectId(ClassName, FuncName);
}

template<typename TObject>
inline uint16 GetCachedRpcFunctionId(uint16& CachedFunctionId, const char* Name)
{
    if (CachedFunctionId != 0)
    {
        return CachedFunctionId;
    }

    CachedFunctionId = GetRpcFunctionIdByName<TObject>(Name);
    return CachedFunctionId;
}

#define MGET_RPC_FUNCTION_ID(ClassType, FuncName) \
    GetRpcFunctionIdByName<ClassType>(#FuncName)

#define MGET_CACHED_RPC_FUNCTION_ID(ClassType, FuncName, CacheVar) \
    GetCachedRpcFunctionId<ClassType>(CacheVar, #FuncName)

#define MGET_STABLE_RPC_FUNCTION_ID(ClassNameLiteral, FuncNameLiteral) \
    GetStableRpcFunctionIdByName(ClassNameLiteral, FuncNameLiteral)

// ============================================
// 属性模板：默认行为 + 容器特化
// ============================================

// 前向声明：用于属性模板特化
template<typename T>
struct TPropertySerializer;

template<typename T>
class TProperty : public MProperty
{
public:
    TProperty(const FString& InName, EPropertyType InType, size_t InOffset, size_t InSize, EPropertyFlags InFlags)
        : MProperty(InName, InType, InOffset, InSize, std::type_index(typeid(T)))
    {
        Flags = InFlags;
    }

    TProperty(
        const FString& InName,
        EPropertyType InType,
        size_t InOffset,
        size_t InSize,
        EPropertyFlags InFlags,
        MutableAccessor InMutableAccessor,
        ConstAccessor InConstAccessor)
        : MProperty(InName, InType, InOffset, InSize, std::type_index(typeid(T)), InMutableAccessor, InConstAccessor)
    {
        Flags = InFlags;
    }

    virtual void SerializeValue(void* Object, MReflectArchive& Ar) const override
    {
        TPropertySerializer<T>::Serialize(this, Object, Ar);
    }
};

template<typename TObject, typename TValue, TValue TObject::* MemberPtr>
class TMemberProperty : public TProperty<TValue>
{
public:
    TMemberProperty(const FString& InName, EPropertyType InType, EPropertyFlags InFlags)
        : TProperty<TValue>(
            InName,
            InType,
            0,
            sizeof(TValue),
            InFlags,
            &TMemberProperty::GetMutableValue,
            &TMemberProperty::GetConstValue)
    {
    }

private:
    static void* GetMutableValue(void* Object)
    {
        return &(static_cast<TObject*>(Object)->*MemberPtr);
    }

    static const void* GetConstValue(const void* Object)
    {
        return &(static_cast<const TObject*>(Object)->*MemberPtr);
    }
};

// 默认序列化：退回到 MProperty 的基础实现
template<typename T>
struct TPropertySerializer
{
    static void Serialize(const MProperty* Prop, void* Object, MReflectArchive& Ar)
    {
        if (!Prop)
        {
            return;
        }
        const_cast<MProperty*>(Prop)->MProperty::SerializeValue(Object, Ar);
    }
};

// TVector 容器专用序列化
template<typename TElement>
struct TPropertySerializer<TVector<TElement>>
{
    static void Serialize(const MProperty* Prop, void* Object, MReflectArchive& Ar)
    {
        if (!Prop || !Object)
        {
            return;
        }

        auto* Vec = Prop->GetValuePtr<TVector<TElement>>(Object);
        if (!Vec)
        {
            return;
        }
        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(Vec->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            Vec->resize(Count);
        }

        if (Count == 0)
        {
            return;
        }

        if constexpr (std::is_trivially_copyable_v<TElement>)
        {
            // POD 元素：按字节批量序列化整个数组
            Ar.SerializeBytes(Vec->data(), sizeof(TElement) * Count);
        }
        else
        {
            // 非 POD：逐个元素走各自的 operator<<
            for (uint32 Index = 0; Index < Count; ++Index)
            {
                TElement& Element = (*Vec)[Index];
                Ar << Element;
            }
        }
    }
};

// TMap<K, V> 容器专用序列化
template<typename K, typename V, typename Compare>
struct TPropertySerializer<TMap<K, V, Compare>>
{
    static void Serialize(const MProperty* Prop, void* Object, MReflectArchive& Ar)
    {
        if (!Prop || !Object)
        {
            return;
        }

        auto* MapPtr = Prop->GetValuePtr<TMap<K, V, Compare>>(Object);
        if (!MapPtr)
        {
            return;
        }
        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(MapPtr->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            MapPtr->clear();
            for (uint32 Index = 0; Index < Count; ++Index)
            {
                K Key{};
                V Value{};
                Ar << Key;
                Ar << Value;
                MapPtr->emplace(std::move(Key), std::move(Value));
            }
        }
        else
        {
            for (auto& Pair : *MapPtr)
            {
                K KeyCopy = Pair.first;
                V& Value = Pair.second;
                Ar << KeyCopy;
                Ar << Value;
            }
        }
    }
};

// TSet<T> 容器专用序列化
template<typename T, typename Compare>
struct TPropertySerializer<TSet<T, Compare>>
{
    static void Serialize(const MProperty* Prop, void* Object, MReflectArchive& Ar)
    {
        if (!Prop || !Object)
        {
            return;
        }

        auto* SetPtr = Prop->GetValuePtr<TSet<T, Compare>>(Object);
        if (!SetPtr)
        {
            return;
        }
        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(SetPtr->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            SetPtr->clear();
            for (uint32 Index = 0; Index < Count; ++Index)
            {
                T Value{};
                Ar << Value;
                SetPtr->insert(std::move(Value));
            }
        }
        else
        {
            for (const T& Value : *SetPtr)
            {
                T Copy = Value;
                Ar << Copy;
            }
        }
    }
};

// 向反射系统注册 TVector 容器属性（元素类型必须已被 MReflectArchive 支持）
template<typename TElement>
class MVectorProperty : public MProperty
{
public:
    MVectorProperty(const FString& InName, size_t InOffset, EPropertyFlags InFlags)
        : MProperty(InName,
                    EPropertyType::None,
                    InOffset,
                    sizeof(TVector<TElement>),
                    std::type_index(typeid(TVector<TElement>)))
    {
        Flags = InFlags;
    }

    virtual void SerializeValue(void* Object, MReflectArchive& Ar) const override
    {
        if (!Object)
        {
            return;
        }

        auto* Vec = reinterpret_cast<TVector<TElement>*>(reinterpret_cast<uint8*>(Object) + Offset);
        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(Vec->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            Vec->resize(Count);
        }

        for (uint32 Index = 0; Index < Count; ++Index)
        {
            TElement& Element = (*Vec)[Index];
            Ar << Element;
        }
    }
};

template<typename TObject, typename TElement, TVector<TElement> TObject::* MemberPtr>
class TMemberVectorProperty : public MProperty
{
public:
    TMemberVectorProperty(const FString& InName, EPropertyFlags InFlags)
        : MProperty(
            InName,
            EPropertyType::None,
            0,
            sizeof(TVector<TElement>),
            std::type_index(typeid(TVector<TElement>)),
            &TMemberVectorProperty::GetMutableValue,
            &TMemberVectorProperty::GetConstValue)
    {
        Flags = InFlags;
    }

    virtual void SerializeValue(void* Object, MReflectArchive& Ar) const override
    {
        auto* Vec = GetValuePtr<TVector<TElement>>(Object);
        if (!Vec)
        {
            return;
        }

        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(Vec->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            Vec->resize(Count);
        }

        for (uint32 Index = 0; Index < Count; ++Index)
        {
            TElement& Element = (*Vec)[Index];
            Ar << Element;
        }
    }

private:
    static void* GetMutableValue(void* Object)
    {
        return &(static_cast<TObject*>(Object)->*MemberPtr);
    }

    static const void* GetConstValue(const void* Object)
    {
        return &(static_cast<const TObject*>(Object)->*MemberPtr);
    }
};

#define REGISTER_TVECTOR_PROPERTY(ElementCppType, PropName, PropFlags) \
    do { \
        auto* Prop = new TMemberVectorProperty<ThisClass, ElementCppType, &ThisClass::PropName>(#PropName, PropFlags); \
        InClass->RegisterProperty(Prop); \
    } while(0)
