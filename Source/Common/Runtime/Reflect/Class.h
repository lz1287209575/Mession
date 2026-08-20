#pragma once

#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Reflect/Property.h"
#include <stdexcept>
#include <typeindex>

// Forward declaration to avoid pulling in MAsync.h's AppMessages.h chain here.
// `SFutureResult<TByteArray>` is the actual handler return type; the full
// typedef + impl live in MAsync.h (loaded transitively where used).
template <typename T> struct SFutureResult;

class MObject;
class MReflectArchive;

inline uint32 ComputeStableReflectHash32(const char* ScopeName, const char* MemberName, const char* Separator = "::") {
    constexpr uint32 OffsetBasis = 2166136261u;
    constexpr uint32 Prime       = 16777619u;

    uint32 Hash      = OffsetBasis;
    auto   MixString = [&Hash](const char* Text) {
        if (!Text) {
            return;
        }

        while (*Text) {
            Hash ^= static_cast<uint8>(*Text);
            Hash *= Prime;
            ++Text;
        }
    };

    MixString(ScopeName);
    if (MemberName) {
        MixString(Separator);
        MixString(MemberName);
    }

    return (Hash == 0) ? 1u : Hash;
}

inline uint16 ComputeStableReflectId(const char* ScopeName, const char* MemberName) {
    const uint32 Hash   = ComputeStableReflectHash32(ScopeName, MemberName);
    uint16       Folded = static_cast<uint16>((Hash >> 16) ^ (Hash & 0xFFFFu));
    return (Folded == 0) ? 1u : Folded;
}

inline uint16 ComputeStableClientFunctionId(const char* ClientApiName) {
    return ComputeStableReflectId("MClientApi", ClientApiName);
}

inline uint32 ComputeStableAssetTypeId(const char* TypeName) {
    return ComputeStableReflectHash32(TypeName, nullptr, nullptr);
}

inline uint32 ComputeStableAssetFieldId(const char* TypeName, const char* FieldName) {
    return ComputeStableReflectHash32(TypeName, FieldName, ".");
}

// 函数标志
enum class EFunctionFlags : uint32 {
    None              = 0,
    Final             = 1 << 0,
    RequiredAPI       = 1 << 1,
    BlueprintCallable = 1 << 2,
    BlueprintPure     = 1 << 3,
    EditorOnly        = 1 << 4,
    Const             = 1 << 5,
    NetServer         = 1 << 6,
    NetClient         = 1 << 7,
    NetReliable       = 1 << 8,
    WithValidation    = 1 << 9,
};

// RPC 类型
enum class ERpcType : uint8 { None = 0, Server = 1, Client = 2, Multicast = 3, ServerToServer = 4 };

enum class EClassKind : uint8 {
    Object      = 0,
    Server      = 1,
    Service     = 2,
    Rpc         = 3,
    Struct      = 4,
    Actor       = 5, // Type = Actor:actor 容器(IActor + MObject,成员挂载点)
    ActorMember = 6, // Type = ActorMember:业务成员(IActorMember,协议下沉至此)
};

class MEnumValue {
    public:
    MString Name;
    int64   Value = 0;
};

class MEnum {
    private:
    MString             EnumName;
    MString             EnumPath;
    uint16              EnumId       = 0;
    std::type_index     CppTypeIndex = typeid(void);
    TVector<MEnumValue> Values;

    public:
    MEnum() = default;
    MEnum(const MString& InName, const MString& InPath, const std::type_index& InCppTypeIndex = typeid(void)) : EnumName(InName), EnumPath(InPath), EnumId(ComputeStableReflectId("MEnum", InName.c_str())), CppTypeIndex(InCppTypeIndex) {
    }

    const MString& GetName() const {
        return EnumName;
    }
    const MString& GetPath() const {
        return EnumPath;
    }
    uint16 GetId() const {
        return EnumId;
    }
    const std::type_index& GetCppTypeIndex() const {
        return CppTypeIndex;
    }
    const TVector<MEnumValue>& GetValues() const {
        return Values;
    }

    void AddValue(const MString& InName, int64 InValue) {
        Values.push_back(MEnumValue{InName, InValue});
    }

    const MEnumValue* FindValue(const MString& InName) const {
        for (const MEnumValue& Value : Values) {
            if (Value.Name == InName) {
                return &Value;
            }
        }
        return nullptr;
    }

    const MEnumValue* FindValueByIntegral(int64 InValue) const {
        for (const MEnumValue& Value : Values) {
            if (Value.Value == InValue) {
                return &Value;
            }
        }
        return nullptr;
    }
};

// step-2: EGeneratedClientCallHandlerResult / FClientParamBinder / FClientCallHandler
// 全部随 RpcDispatch 重构删除。原 client 侧 dispatch 由 Gateway 端
// 反射 (MClientManifest::FindByFunctionId + MRpcChannel::Call) 接管。
// server-side ServerCall 路径仍保留(FServerCallHandler + ServerCallHandler)。

class MFunctionObject {
    public:
    using FServerCallHandler = SFutureResult<TByteArray> (*)(MObject* Object, const TByteArray& Payload);

    MString            Name;
    EFunctionFlags     Flags              = EFunctionFlags::None;
    uint16             FunctionId         = 0;
    size_t             ParamSize          = 0;
    EServerType        EndpointServerType = EServerType::Unknown;
    MString            ClientApiName;
    MString            Transport;
    FServerCallHandler ServerCallHandler = nullptr;

    using FunctionPtr          = void (*)(void*);
    FunctionPtr NativeFunc     = nullptr;
    using NativeInvoker        = bool (*)(MObject*, MReflectArchive*, MReflectArchive*);
    NativeInvoker NativeInvoke = nullptr;

    ERpcType RpcType   = ERpcType::None;
    bool     bReliable = true;

    TVector<MProperty*> Params;
    MProperty*          ReturnProperty = nullptr;

    // Back-pointer to the owning MClass. Set by MClass::RegisterFunction.
    // Used by callers (e.g. Lua engine fallback) that need both the function
    // name and its class to locate a class.method global in the script VM.
    class MClass* OwnerClass = nullptr;

    MFunctionObject() = default;
    virtual ~MFunctionObject() {
        for (MProperty* Param : Params) {
            delete Param;
        }
        delete ReturnProperty;
    }
};
// Backward-compatible alias for code that uses MFunction as a type name
using MFunction = MFunctionObject;

class MClass {
    private:
    inline static uint16 GlobalClassId    = 0;
    inline static uint16 GlobalPropertyId = 0;
    inline static uint16 GlobalFunctionId = 0;

    protected:
    MString         ClassName;
    MString         ClassPath;
    uint16          ClassId      = 0;
    uint32          AssetTypeId  = 0;
    std::type_index CppTypeIndex = typeid(void);

    TVector<MProperty*> Properties;
    TVector<MFunction*> Functions;

    MClass* ParentClass = nullptr;

    using ClassConstructor       = void* (*)(void*);
    ClassConstructor Constructor = nullptr;
    using ClassDestructor        = void (*)(void*);
    ClassDestructor Destructor   = nullptr;

    uint32     ClassFlags = 0;
    EClassKind ClassKind  = EClassKind::Object;

    public:
    MClass();
    virtual ~MClass();

    const MString& GetName() const {
        return ClassName;
    }
    const MString& GetPath() const {
        return ClassPath;
    }
    uint16 GetId() const {
        return ClassId;
    }
    uint32 GetAssetTypeId() const {
        return AssetTypeId;
    }
    const std::type_index& GetCppTypeIndex() const {
        return CppTypeIndex;
    }
    const MClass* GetParentClass() const {
        return ParentClass;
    }

    const TVector<MProperty*>& GetProperties() const {
        return Properties;
    }
    MProperty* FindProperty(const MString& InName) const;
    MProperty* FindPropertyById(uint16 InId) const;
    MProperty* FindPropertyByAssetFieldId(uint32 InAssetFieldId) const;
    MProperty* FindPropertyByMetadata(const MString& InKey, const MString& InValue) const;

    const TVector<MFunction*>& GetFunctions() const {
        return Functions;
    }
    MFunction* FindFunction(const MString& InName) const;
    MFunction* FindFunctionById(uint16 InId) const;

    void* CreateInstance() const;
    void  DestroyInstance(void* Object) const;
    void  Construct(void* Object) const;

    virtual void WriteSnapshot(void* Object, class MReflectArchive& Ar) const;
    virtual void WriteSnapshotByDomain(void* Object, class MReflectArchive& Ar, uint64 InDomainMask) const;
    virtual void ReadSnapshot(void* Object, const TByteArray& Data) const;
    virtual void ReadSnapshotByDomain(void* Object, const TByteArray& Data, uint64 InDomainMask) const;

    // 继承序列化辅助:父类与本类共享同一 MReflectArchive 游标(先父后子,
    // 对称于 WriteSnapshot 的递归顺序)。读入口 ReadSnapshot/ByDomain 建
    // Ar 后转调这里,避免递归重建 Ar 导致游标归零、字段错位。
    void ReadSnapshotFields(void* Object, class MReflectArchive& Ar) const;
    void ReadSnapshotFieldsByDomain(void* Object, class MReflectArchive& Ar, uint64 InDomainMask) const;
    MString      ExportObjectToString(const void* Object) const;

    void CopyProperties(void* Dest, const void* Src) const;

    bool HasFlags(uint32 InFlags) const {
        return (ClassFlags & InFlags) != 0;
    }
    EClassKind GetKind() const {
        return ClassKind;
    }

    template <typename TObject> void SetConstructor() {
        Constructor = [](void* InPlace) -> void* {
            if (InPlace) {
                return new (InPlace) TObject();
            }
            return new TObject();
        };
        Destructor = [](void* Object) {
            if (!Object) {
                return;
            }
            delete static_cast<TObject*>(Object);
        };
    }

    void SetMeta(const MString& InName, const MString& InPath, MClass* InParent, uint32 InFlags) {
        ClassName   = InName;
        ClassPath   = InPath;
        ParentClass = InParent;
        ClassFlags  = InFlags;
        AssetTypeId = ComputeStableAssetTypeId(ClassName.c_str());
    }

    // 继承序列化支持:生成器在 SetMeta 之后单独设置反射父类(MSTRUCT 继承
    // 链如 FPlayerRequestBase ← FPlayerUseItemRequest;WriteSnapshot/ReadSnapshot
    // 先父后子递归,见 Class.cpp)。SetMeta 的 InParent 恒传 nullptr 的历史
    // 行为保留——父类由生成器这里显式接线。
    void SetParent(MClass* InParent) {
        ParentClass = InParent;
    }

    void SetKind(EClassKind InKind) {
        ClassKind = InKind;
    }

    void SetCppTypeIndex(const std::type_index& InCppTypeIndex) {
        CppTypeIndex = InCppTypeIndex;
    }

    void RegisterProperty(MProperty* InProperty) {
        if (!InProperty) {
            return;
        }

        InProperty->PropertyId   = ++GlobalPropertyId;
        InProperty->AssetFieldId = ComputeStableAssetFieldId(ClassName.c_str(), InProperty->Name.c_str());

        if (FindPropertyByAssetFieldId(InProperty->AssetFieldId)) {
            const MString Message = "Reflection asset field id collision: class=" + ClassName + " property=" + InProperty->Name + " asset_field_id=" + std::to_string(InProperty->AssetFieldId);
            LOG_FATAL("%s", Message.c_str());
            throw std::runtime_error(Message);
        }

        uint64 InferredDomains = ToMask(EPropertyDomainFlags::None);
        if (InProperty->HasAnyFlags(EPropertyFlags::Replicated) || InProperty->HasAnyFlags(EPropertyFlags::RepNotify) || InProperty->HasAnyFlags(EPropertyFlags::RepToClient)) {
            InferredDomains |= ToMask(EPropertyDomainFlags::Replication);
        }
        if (InProperty->HasAnyFlags(EPropertyFlags::SaveGame) || InProperty->HasAnyFlags(EPropertyFlags::PersistentData)) {
            InferredDomains |= ToMask(EPropertyDomainFlags::Persistence);
        }
        if (InProperty->HasAnyFlags(EPropertyFlags::Asset)) {
            InferredDomains |= ToMask(EPropertyDomainFlags::Asset);
        }
        InProperty->DomainFlags = InferredDomains;
        Properties.push_back(InProperty);
    }

    void RegisterFunction(MFunction* InFunction) {
        if (InFunction) {
            uint16 StableId = InFunction->FunctionId;
            if (StableId == 0) {
                if (!InFunction->ClientApiName.empty()) {
                    StableId = ComputeStableClientFunctionId(InFunction->ClientApiName.c_str());
                } else {
                    StableId = ComputeStableReflectId(ClassName.c_str(), InFunction->Name.c_str());
                }
            }

            if (FindFunctionById(StableId)) {
                LOG_WARN("Reflection function id collision: class=%s function=%s api=%s id=%u", ClassName.c_str(), InFunction->Name.c_str(), InFunction->ClientApiName.c_str(), static_cast<unsigned>(StableId));
            }
            InFunction->FunctionId = StableId;
        }
        InFunction->OwnerClass = this;
        Functions.push_back(InFunction);
    }
};
