#pragma once

#include "Core/Net/NetCore.h"
#include <typeinfo>
#include <typeindex>
#include <cstring>
#include <type_traits>
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

// 属性基础类
class MProperty
{
public:
    FString Name;
    EPropertyType Type = EPropertyType::None;
    EPropertyFlags Flags = EPropertyFlags::None;
    size_t Offset = 0;
    size_t Size = 0;
    uint16 PropertyId = 0;
    std::type_index CppTypeIndex = typeid(void);

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

    virtual ~MProperty() = default;

    // 默认基于 Type 做序列化，容器等复杂类型可在子类中重写
    virtual void SerializeValue(void* Object, MReflectArchive& Ar) const;

    // 获取属性值（模板化）
    template<typename T>
    T* GetValuePtr(void* Object) const
    {
        return reinterpret_cast<T*>(reinterpret_cast<uint8*>(Object) + Offset);
    }

    template<typename T>
    const T* GetValuePtr(const void* Object) const
    {
        return reinterpret_cast<const T*>(reinterpret_cast<const uint8*>(Object) + Offset);
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
        InFunction->FunctionId = ++GlobalFunctionId;
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
#define MPROPERTY(...)
#define MFUNCTION(...)

// 兼容旧的 UE 风格宏，映射到 M 前缀
#define UPROPERTY(...) MPROPERTY(__VA_ARGS__)
#define UFUNCTION(...) MFUNCTION(__VA_ARGS__)

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
#define GENERATED_BODY(ClassName, ParentClass, Flags) \
public: \
    using ThisClass = ClassName; \
    using Super = ParentClass; \
    static MClass* StaticClass(); \
    virtual MClass* GetClass() const override { return StaticClass(); } \
private: \
    static void RegisterAllProperties(MClass* InClass); \
    static void RegisterAllFunctions(MClass* InClass);

// MCLASS / UCLASS：类反射声明宏（别名）
#define MCLASS(ClassName, ParentClass, Flags) \
    GENERATED_BODY(ClassName, ParentClass, Flags)

#define UCLASS(ClassName, ParentClass, Flags) \
    MCLASS(ClassName, ParentClass, Flags)

// 在类外实现反射注册
#define IMPLEMENT_CLASS(ClassName, ParentClass, Flags) \
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
#define REGISTER_PROPERTY(CppType, PropEnum, PropName, PropFlags) \
    do { \
        auto* Prop = new TProperty<CppType>(#PropName, EPropertyType::PropEnum, offsetof(ThisClass, PropName), sizeof(CppType), PropFlags); \
        InClass->RegisterProperty(Prop); \
    } while(0)

// 向反射系统注册 TVector 容器属性（元素类型必须已被 MReflectArchive 支持）

// 注册函数宏
#define REGISTER_FUNCTION(FuncName, FuncFlags) \
    do { \
        auto* Func = new MFunction(); \
        Func->Name = #FuncName; \
        Func->Flags = EFunctionFlags::FuncFlags; \
        InClass->RegisterFunction(Func); \
    } while(0)

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

    virtual void SerializeValue(void* Object, MReflectArchive& Ar) const override
    {
        TPropertySerializer<T>::Serialize(this, Object, Ar);
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

        auto* Vec = reinterpret_cast<TVector<TElement>*>(reinterpret_cast<uint8*>(Object) + Prop->Offset);
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

        auto* MapPtr = reinterpret_cast<TMap<K, V, Compare>*>(reinterpret_cast<uint8*>(Object) + Prop->Offset);
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

        auto* SetPtr = reinterpret_cast<TSet<T, Compare>*>(reinterpret_cast<uint8*>(Object) + Prop->Offset);
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

#define REGISTER_TVECTOR_PROPERTY(ElementCppType, PropName, PropFlags) \
    do { \
        auto* Prop = new MVectorProperty<ElementCppType>(#PropName, offsetof(ThisClass, PropName), PropFlags); \
        InClass->RegisterProperty(Prop); \
    } while(0)
