#pragma once

#include "../Core/NetCore.h"
#include <typeinfo>
#include <typeindex>
#include <map>
#include <functional>
#include <vector>
#include <string>

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
    Enum = 19
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

// 属性基础类
class FProperty
{
public:
    FString Name;
    EPropertyType Type = EPropertyType::None;
    EPropertyFlags Flags = EPropertyFlags::None;
    size_t Offset = 0;
    size_t Size = 0;
    uint16 PropertyId = 0;
    
    FProperty() = default;
    FProperty(const FString& InName, EPropertyType InType, size_t InOffset, size_t InSize)
        : Name(InName), Type(InType), Offset(InOffset), Size(InSize) {}
    
    virtual ~FProperty() = default;
    
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
class UFunction
{
public:
    FString Name;
    EFunctionFlags Flags = EFunctionFlags::None;
    uint16 FunctionId = 0;
    size_t ParamSize = 0;
    
    // 函数指针类型
    using FunctionPtr = void(*)(void*);
    FunctionPtr NativeFunc = nullptr;
    
    // 参数列表
    std::vector<FProperty*> Params;
    FProperty* ReturnProperty = nullptr;
    
    UFunction() = default;
    virtual ~UFunction() = default;
};

// 类元数据
class UClass
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
    std::vector<FProperty*> Properties;
    std::vector<UFunction*> Functions;
    
    // 父类
    UClass* ParentClass = nullptr;
    
    // 创建函数
    using ClassConstructor = void*(*)(void*);
    ClassConstructor Constructor = nullptr;
    
    // 类标志
    uint32 ClassFlags = 0;
    
public:
    UClass() 
    {
        ClassId = ++GlobalClassId;
    }
    
    virtual ~UClass() 
    {
        for (auto* Prop : Properties)
            delete Prop;
        for (auto* Func : Functions)
            delete Func;
    }
    
    // 获取类名
    const FString& GetName() const { return ClassName; }
    const FString& GetPath() const { return ClassPath; }
    uint16 GetId() const { return ClassId; }
    
    // 获取属性
    const std::vector<FProperty*>& GetProperties() const { return Properties; }
    FProperty* FindProperty(const FString& InName) const;
    FProperty* FindPropertyById(uint16 InId) const;
    
    // 获取 std::vector<UFunction*>
    constFunction*>& GetFunctions() const { return Functions; }
    UFunction* FindFunction(const FString& InName) const;
    UFunction* FindFunctionById(uint16 InId) const;
    
    // 创建实例
    void* CreateInstance() const;
    void Construct(void* Object) const;
    
    // 序列化
    virtual void Serialize(void* Object, class FArchive& Ar) const;
    virtual void Deserialize(void* Object, const TArray& Data) const;
    
    // 复制属性
    void CopyProperties(void* Dest, const void* Src) const;
    
    // 检查类标志
    bool HasFlags(uint32 InFlags) const { return (ClassFlags & InFlags) != 0; }
    
protected:
    // 注册属性（子类调用）
    void RegisterProperty(FProperty* InProperty)
    {
        InProperty->PropertyId = ++GlobalPropertyId;
        Properties.push_back(InProperty);
    }
    
    // 注册函数（子类调用）
    void RegisterFunction(UFunction* InFunction)
    {
        InFunction->FunctionId = ++GlobalFunctionId;
        Functions.push_back(InFunction);
    }
};

// 查找实现
inline FProperty* UClass::FindProperty(const FString& InName) const
{
    for (auto* Prop : Properties)
    {
        if (Prop->Name == InName)
            return Prop;
    }
    if (ParentClass)
        return ParentClass->FindProperty(InName);
    return nullptr;
}

inline FProperty* UClass::FindPropertyById(uint16 InId) const
{
    for (auto* Prop : Properties)
    {
        if (Prop->PropertyId == InId)
            return Prop;
    }
    if (ParentClass)
        return ParentClass->FindPropertyById(InId);
    return nullptr;
}

inline UFunction* UClass::FindFunction(const FString& InName) const
{
    for (auto* Func : Functions)
    {
        if (Func->Name == InName)
            return Func;
    }
    if (ParentClass)
        return ParentClass->FindFunction(InName);
    return nullptr;
}

inline UFunction* UClass::FindFunctionById(uint16 InId) const
{
    for (auto* Func : Functions)
    {
        if (Func->FunctionId == InId)
            return Func;
    }
    if (ParentClass)
        return ParentClass->FindFunctionById(InId);
    return nullptr;
}

// ============================================
// UObject - 反射系统的基类
// ============================================

class UObject
{
private:
    inline static std::map<FString, UClass*>& GetClassMap()
    {
        static std::map<FString, UClass*> Map;
        return Map;
    }
    
    inline static std::map<uint16, UClass*>& GetClassIdMap()
    {
        static std::map<uint16, UClass*> Map;
        return Map;
    }

protected:
    UClass* Class = nullptr;
    uint64 ObjectFlags = 0;
    uint64 ObjectId = 0;
    FString Name;
    
public:
    UObject() : ObjectId(++GlobalObjectId) {}
    virtual ~UObject() = default;
    
    // 获取类和对象信息
    UClass* GetClass() const { return Class; }
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
        if (!Class) return nullptr;
        auto* Prop = Class->FindProperty(InName);
        if (!Prop) return nullptr;
        return Prop->GetValuePtr<T>(const_cast<void*>(static_cast<const void*>(this)));
    }
    
    template<typename T>
    void SetProperty(const FString& InName, const T& Value)
    {
        if (!Class) return;
        auto* Prop = Class->FindProperty(InName);
        if (!Prop) return;
        *Prop->GetValuePtr<T>(this) = Value;
    }
    
    bool CallFunction(const FString& InName);
    
    // 静态方法：类注册
    template<typename T>
    static UClass* StaticClass()
    {
        return T::StaticClass();
    }
    
    static UClass* FindClass(const FString& InName)
    {
        auto It = GetClassMap().find(InName);
        return (It != GetClassMap().end()) ? It->second : nullptr;
    }
    
    static UClass* FindClass(uint16 InId)
    {
        auto It = GetClassIdMap().find(InId);
        return (It != GetClassIdMap().end()) ? It->second : nullptr;
    }
    
    static void RegisterClass(UClass* InClass)
    {
        GetClassMap()[InClass->GetName()] = InClass;
        GetClassIdMap()[InClass->GetId()] = InClass;
    }
    
private:
    inline static uint64 GlobalObjectId = 0;
};

// ============================================
// 宏定义 - 简化版
// ============================================

// 注册属性
#define PROPERTY(Type, Name, Flags) \
    struct FProperty_##Name : public FProperty { \
        FProperty_##Name() : FProperty(#Name, EPropertyType::Type, offsetof(ThisClass, Name), sizeof(Type)) { \
            this->Flags = EPropertyFlags::Flags; \
        } \
    } Name##_Property;

// 注册函数
#define FUNCTION(Name, Flags) \
    struct UFunction_##Name : public UFunction { \
        UFunction_##Name() { \
            Name = #Name; \
            Flags = EFunctionFlags::Flags; \
        } \
    };

// 定义UCLASS
#define UCLASS(ClassName, ParentClass, Flags) \
    class ClassName : public ParentClass { \
    public: \
        using ThisClass = ClassName; \
        static UClass* StaticClass() { \
            static UClass* Class = nullptr; \
            if (!Class) { \
                Class = new UClass(); \
                Class->ClassName = #ClassName; \
                Class->ClassPath = __FILE__; \
                Class->ParentClass = ParentClass::StaticClass(); \
                Class->ClassFlags = Flags; \
                UObject::RegisterClass(Class); \
                RegisterAllProperties(Class); \
                RegisterAllFunctions(Class); \
            } \
            return Class; \
        } \
    private: \
        static void RegisterAllProperties(UClass* InClass); \
        static void RegisterAllFunctions(UClass* InClass);

// 结束类定义
#define END_UCLASS() \
    };

// 注册属性宏
#define REGISTER_PROPERTY(PropType, PropName, PropFlags) \
    do { \
        auto* Prop = new FProperty(#PropName, EPropertyType::PropType, offsetof(ThisClass, PropName), sizeof(PropType)); \
        Prop->Flags = EPropertyFlags::PropFlags; \
        InClass->RegisterProperty(Prop); \
    } while(0)

// 注册函数宏
#define REGISTER_FUNCTION(FuncName, FuncFlags) \
    do { \
        auto* Func = new UFunction(); \
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

class FArchive
{
public:
    TArray Data;
    size_t ReadPos = 0;
    bool bReading = false;
    bool bWriting = true;
    
    FArchive() { bWriting = true; bReading = false; }
    explicit FArchive(const TArray& InData) : Data(InData), ReadPos(0), bReading(true), bWriting(false) {}
    
    // 序列化基本类型
    FArchive& operator<<(uint8& Value) { return SerializePrimitive(Value); }
    FArchive& operator<<(uint16& Value) { return SerializePrimitive(Value); }
    FArchive& operator<<(uint32& Value) { return SerializePrimitive(Value); }
    FArchive& operator<<(uint64& Value) { return SerializePrimitive(Value); }
    FArchive& operator<<(int8& Value) { return SerializePrimitive(Value); }
    FArchive& operator<<(int16& Value) { return SerializePrimitive(Value); }
    FArchive& operator<<(int32& Value) { return SerializePrimitive(Value); }
    FArchive& operator<<(int64& Value) { return SerializePrimitive(Value); }
    FArchive& operator<<(float& Value) { return SerializePrimitive(Value); }
    FArchive& operator<<(double& Value) { return SerializePrimitive(Value); }
    FArchive& operator<<(FString& Value) { return SerializeString(Value); }
    FArchive& operator<<(FVector& Value) { return SerializePrimitive(Value); }
    FArchive& operator<<(FRotator& Value) { return SerializePrimitive(Value); }
    
private:
    template<typename T>
    FArchive& SerializePrimitive(T& Value)
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
    
    FArchive& SerializeString(FString& Value)
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
