/**
 * @file Class.h
 * @brief 反射元数据:类(MClass)、属性(MProperty 的宿主视图)、函数(MFunction)
 *   与"稳定 id"哈希工具。
 *
 * @detail 稳定 id 的取值只由名字决定(FNV-1a),因此不随编译顺序或注册顺序变化,
 *   可以跨进程/跨版本作为线上标识(属性 AssetFieldId、客户端函数 FunctionId 等);
 *   与之相对,MClass::ClassId / MProperty::PropertyId 是注册顺序自增的运行期编号,
 *   仅用于本进程内查表,不能落盘。
 */
#pragma once

#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Reflect/Property.h"
#include <stdexcept>
#include <typeindex>

// 前向声明,避免在此处引入 MAsync.h 的 AppMessages.h 依赖链。
// SFutureResult<TByteArray> 才是真正的 handler 返回类型,其定义在
// MAsync.h(由使用方传递包含)。
template <typename T> struct SFutureResult;

class MObject;
class MReflectArchive;

/**
 * @brief ComputeStableReflectHash32 - 由"作用域名 + 成员名"算稳定 32 位哈希。
 * @param ScopeName 作用域名(类名/枚举名等),为空则跳过
 * @param MemberName 成员名;为空(nullptr)时只哈希 ScopeName,且不掺入分隔符
 * @param Separator 作用域与成员之间的分隔符,默认 "::"
 * @return FNV-1a 32 位哈希;结果为 0 时改返回 1(0 被保留作"未设置"哨兵)
 *
 * @detail 用分隔符把两段拼在一起再哈希,避免 ("AB","C") 与 ("A","BC") 撞哈希。
 */
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

/**
 * @brief ComputeStableReflectId - 把 32 位稳定哈希折叠成 16 位 id。
 * @param ScopeName 作用域名
 * @param MemberName 成员名
 * @return 高低 16 位异或后的结果;为 0 时改返回 1
 *
 * @detail 线上协议里的 FunctionId/PropertyId 通道只有 16 位,折叠会提高碰撞率;
 *   碰撞由 MClass::RegisterFunction 的告警和 MObject::RegisterClass 的致命错误
 *   在注册期兜底。
 */
inline uint16 ComputeStableReflectId(const char* ScopeName, const char* MemberName) {
    const uint32 Hash   = ComputeStableReflectHash32(ScopeName, MemberName);
    uint16       Folded = static_cast<uint16>((Hash >> 16) ^ (Hash & 0xFFFFu));
    return (Folded == 0) ? 1u : Folded;
}

/**
 * @brief ComputeStableClientFunctionId - 客户端 API 名的稳定函数 id。
 * @param ClientApiName 客户端 API 名
 * @return 固定以 "MClientApi" 作作用域算出的 16 位 id
 */
inline uint16 ComputeStableClientFunctionId(const char* ClientApiName) {
    return ComputeStableReflectId("MClientApi", ClientApiName);
}

/**
 * @brief ComputeStableAssetTypeId - 类型名的稳定资产类型 id。
 * @param TypeName 类名或结构体名
 * @return 不带成员名的 32 位哈希(即只哈希 TypeName)
 *
 * @detail 这是 .mobj 文件头里 RootAssetTypeId 的来源,改名即换 id,
 *   因此已产出的资产文件不跟随重命名。
 */
inline uint32 ComputeStableAssetTypeId(const char* TypeName) {
    return ComputeStableReflectHash32(TypeName, nullptr, nullptr);
}

/**
 * @brief ComputeStableAssetFieldId - 字段的稳定资产字段 id。
 * @param TypeName 所属类/结构体名
 * @param FieldName 字段名
 * @return 以 "." 为分隔符的 32 位哈希
 *
 * @detail 二进制资产按该 id 认字段(而非字段名),因此重命名字段不影响
 *   已产出的文件;但改类型名会同时改变所有字段 id。
 */
inline uint32 ComputeStableAssetFieldId(const char* TypeName, const char* FieldName) {
    return ComputeStableReflectHash32(TypeName, FieldName, ".");
}

/**
 * @brief EFunctionFlags - 函数语义标志位(用于生成器/工具链标注)。
 *
 * @detail 这些位描述"函数是什么",运行期的调用方式则由
 *   MFunctionObject::RpcType / bReliable 决定;两者不要混用。
 */
enum class EFunctionFlags : uint32 {
    None              = 0,
    Final             = 1 << 0, // 不可被派生类重写(仿 UE,仅语义标注)
    RequiredAPI       = 1 << 1, // 跨模块必须导出
    BlueprintCallable = 1 << 2, // 可供脚本层调用(有副作用)
    BlueprintPure     = 1 << 3, // 可供脚本层调用(无副作用)
    EditorOnly        = 1 << 4, // 编辑器专用,不参与运行期分发
    Const             = 1 << 5, // 常量方法
    NetServer         = 1 << 6, // 允许在服务端执行
    NetClient         = 1 << 7, // 允许在客户端执行
    NetReliable       = 1 << 8, // 可靠传输
    WithValidation    = 1 << 9, // 调用前先跑校验函数
};

/** @brief RPC 调用模式;None 表示普通(非 RPC)函数。 */
enum class ERpcType : uint8 { None = 0, Server = 1, Client = 2, Multicast = 3, ServerToServer = 4 };

/**
 * @brief EClassKind - 反射类别的分类,决定实例化的方式与用途。
 *
 * @detail 目前运行期只区分 Struct 与"非 Struct":非 Struct 的实例会被当作
 *   MObject 设置反射类(见 MClass::CreateInstance/Construct)。
 */
enum class EClassKind : uint8 {
    Object      = 0,
    Server      = 1,
    Service     = 2,
    Rpc         = 3,
    Struct      = 4, // 纯数据结构:无 MObject 头,CreateInstance 不做 SetClass
    Actor       = 5, // Type = Actor:actor 容器(IActor + MObject,成员挂载点)
    ActorMember = 6, // Type = ActorMember:业务成员(IActorMember,协议下沉至此)
};

/** @brief 枚举项:名字 + 整数值。 */
class MEnumValue {
    public:
    MString Name;
    int64   Value = 0;
};

/**
 * @brief MEnum - 枚举的反射描述(名字/路径/id/取值表)。
 *
 * @detail EnumId 由名字经 ComputeStableReflectId 算出(稳定、与注册顺序无关);
 *   CppTypeIndex 用于把 C++ 枚举类型反查回本对象(MObject::FindEnum 重载),
 *   未绑定 C++ 类型时保持 typeid(void),此时不会进入类型索引表。
 */
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

    /** @brief 追加一个枚举项;不查重,同名项会同时存在。 */
    void AddValue(const MString& InName, int64 InValue) {
        Values.push_back(MEnumValue{InName, InValue});
    }

    /** @brief 按名字查找枚举项,未命中返回 nullptr。 */
    const MEnumValue* FindValue(const MString& InName) const {
        for (const MEnumValue& Value : Values) {
            if (Value.Name == InName) {
                return &Value;
            }
        }
        return nullptr;
    }

    /** @brief 按整数值查找枚举项(线性扫描,取首个匹配),未命中返回 nullptr。 */
    const MEnumValue* FindValueByIntegral(int64 InValue) const {
        for (const MEnumValue& Value : Values) {
            if (Value.Value == InValue) {
                return &Value;
            }
        }
        return nullptr;
    }
};

// step-2 随 RpcDispatch 重构删除的整套类型:EGeneratedClientCallHandlerResult /
// FClientParamBinder / FClientCallHandler。
// 原 client 侧 dispatch 由 Gateway 端反射
// (MClientManifest::FindByFunctionId + MRpcChannel::Call) 接管。
// server-side ServerCall 路径仍保留(FServerCallHandler + ServerCallHandler)。

/**
 * @brief MFunctionObject - 函数的反射描述(名字/id/参数/调用入口)。
 *
 * @detail 调用入口二选一或并存:
 *   - ServerCallHandler:生成器产出的服务端入口,入参是已序列化的 payload 字节
 *   - NativeInvoke:模板生成的"反序列化参数 → 调用成员函数 → 序列化返回值"跳板
 *   - NativeFunc:只接受对象指针的极简签名,仅在无参且无返回时才被采用
 *   ParamSize/Params/ReturnProperty 描述形参,Params 与 ReturnProperty 由本对象
 *   拥有(析构时 delete),加入时不要重复 delete。
 */
class MFunctionObject {
    public:
    /** @brief 服务端调用入口的签名:对象 + 序列化 payload → 异步响应体。 */
    using FServerCallHandler = SFutureResult<TByteArray> (*)(MObject* Object, const TByteArray& Payload);

    MString            Name; // 函数名(注册时由宏的字符串化得到)
    EFunctionFlags     Flags              = EFunctionFlags::None;
    uint16             FunctionId         = 0;                    // 注册时算出:优先 ClientApiName,否则 类名::函数名
    size_t             ParamSize          = 0;                    // 形参字节数(序列化后大小),0 表示无参
    EServerType        EndpointServerType = EServerType::Unknown; // FOR_SERVER 注册时指定的目标服务类型
    MString            ClientApiName;                             // 客户端 API 名,非空则 FunctionId 走客户端命名空间
    MString            Transport;                                 // 传输通道名,如 "ServerCall"(见 RpcServerCall.cpp)
    FServerCallHandler ServerCallHandler = nullptr;

    using FunctionPtr          = void (*)(void*);
    FunctionPtr NativeFunc     = nullptr;
    using NativeInvoker        = bool (*)(MObject*, MReflectArchive*, MReflectArchive*);
    NativeInvoker NativeInvoke = nullptr;

    ERpcType RpcType   = ERpcType::None; // None = 普通函数,非 None 走 RPC 分发
    bool     bReliable = true;           // RPC 是否可靠传输

    TVector<MProperty*> Params;                   // 形参描述,本对象拥有
    MProperty*          ReturnProperty = nullptr; // 返回值描述;无返回值为空,本对象拥有

    // 指向所属 MClass 的反向指针,由 MClass::RegisterFunction 设置。
    // 供需要"类名 + 函数名"才能定位目标的调用方使用(例如 Lua 引擎回退路径
    // 要在脚本 VM 里找 class.method 全局符号)。
    class MClass* OwnerClass = nullptr;

    MFunctionObject() = default;
    virtual ~MFunctionObject() {
        for (MProperty* Param : Params) {
            delete Param;
        }
        delete ReturnProperty;
    }
};
// 兼容旧代码:仍可用 MFunction 作为类型名
using MFunction = MFunctionObject;

/**
 * @brief MClass - 类/结构体的反射描述(元数据 + 属性/函数表 + 构造析构)。
 *
 * @detail 生命周期与所有权:
 *   - MClass 实例由生成代码在 StaticClass() 里 new 一次并常驻(进程级),不由反射系统释放
 *   - Properties/Functions 里的对象归本类所有,析构时统一 delete
 *   - ParentClass 仅作链接(不拥有),用于属性/函数查找的继承链回溯
 *   - 非线程安全:id 分配与注册都假定在启动期单线程完成
 */
class MClass {
    private:
    inline static uint16 GlobalClassId    = 0;
    inline static uint16 GlobalPropertyId = 0;
    inline static uint16 GlobalFunctionId = 0;

    protected:
    MString         ClassName;
    MString         ClassPath;        // 目前由 MIMPLEMENT_CLASS 传入 __FILE__
    uint16          ClassId      = 0; // 注册顺序自增的运行期编号(构造时取 ++GlobalClassId)
    uint32          AssetTypeId  = 0; // 由 ClassName 派生的稳定 id,见 ComputeStableAssetTypeId
    std::type_index CppTypeIndex = typeid(void);

    TVector<MProperty*> Properties; // 仅本类自有属性(不含父类),本类拥有
    TVector<MFunction*> Functions;  // 仅本类自有函数,本类拥有

    MClass* ParentClass = nullptr; // 反射父类(不拥有);由生成器在 SetMeta 之后用 SetParent 接线

    using ClassConstructor       = void* (*)(void*);
    ClassConstructor Constructor = nullptr; // 传入内存则原地构造,否则 new
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

    /** @brief 本类自有属性列表(不含父类);父类属性需自行沿链回溯。 */
    const TVector<MProperty*>& GetProperties() const {
        return Properties;
    }
    /** @brief 按名字查找属性:本类优先,未命中再回溯父类;未找到返回 nullptr。 */
    MProperty* FindProperty(const MString& InName) const;
    /** @brief 按属性 id(注册顺序编号)查找:本类优先,再回溯父类。 */
    MProperty* FindPropertyById(uint16 InId) const;
    /** @brief 按资产字段 id(稳定哈希)查找:本类优先,再回溯父类。 */
    MProperty* FindPropertyByAssetFieldId(uint32 InAssetFieldId) const;
    /**
     * @brief 按元数据键值查找属性。
     * @param InKey 元数据键
     * @param InValue 期望值;为空串时只要存在该键即视为命中
     * @return 命中属性;本类优先,再回溯父类;未找到返回 nullptr
     */
    MProperty* FindPropertyByMetadata(const MString& InKey, const MString& InValue) const;

    /** @brief 本类自有函数列表(不含父类)。 */
    const TVector<MFunction*>& GetFunctions() const {
        return Functions;
    }
    /** @brief 按名字查找函数:本类优先,再回溯父类。 */
    MFunction* FindFunction(const MString& InName) const;
    /** @brief 按 FunctionId 查找函数:本类优先,再回溯父类。 */
    MFunction* FindFunctionById(uint16 InId) const;

    /**
     * @brief 创建并初始化一个实例。
     * @return 新实例;未设置构造函数(如纯元数据类)时返回 nullptr
     *
     * @detail 非 Struct 类别会被当作 MObject 调 SetClass(this),因此调用方拿到
     *   的就是已绑定反射信息的对象;实例的释放必须走 DestroyInstance。
     */
    void* CreateInstance() const;
    /** @brief 销毁 CreateInstance 产生的实例;Object 为空或未设析构时不做任何事。 */
    void DestroyInstance(void* Object) const;
    /**
     * @brief 在已有内存上原地构造实例(placement new)。
     * @param Object 已分配的内存;为空或未设构造函数时不做任何事
     *
     * @detail 非 Struct 类别同样会补一次 SetClass。注意本函数不返回对象,
     *   与 CreateInstance 共用同一个 Constructor 但语义是"填充已有内存"。
     */
    void Construct(void* Object) const;

    virtual void WriteSnapshot(void* Object, class MReflectArchive& Ar) const;
    virtual void WriteSnapshotByDomain(void* Object, class MReflectArchive& Ar, uint64 InDomainMask) const;
    virtual void ReadSnapshot(void* Object, const TByteArray& Data) const;
    virtual void ReadSnapshotByDomain(void* Object, const TByteArray& Data, uint64 InDomainMask) const;

    // 继承序列化辅助:父类与本类共享同一 MReflectArchive 游标(先父后子,
    // 对称于 WriteSnapshot 的递归顺序)。读入口 ReadSnapshot/ByDomain 建
    // Ar 后转调这里,避免递归重建 Ar 导致游标归零、字段错位。
    void ReadSnapshotFields(void* Object, class MReflectArchive& Ar) const;
    void ReadSnapshotFieldsByDomain(void* Object, class MReflectArchive& Ar, uint64 InDomainMask) const;
    /**
     * @brief 把对象的属性渲染成 "类名{字段=值, ...}" 文本,用于日志/调试。
     * @param Object 待渲染对象;为空返回 "类名{<null>}"
     *
     * @detail 只拼接"直接父类的自有属性 + 本类自有属性",不递归到更高祖先,
     *   因此三层以上继承链的字段不会出现在文本里(与 WriteSnapshot 的完整递归不同)。
     */
    MString ExportObjectToString(const void* Object) const;

    /**
     * @brief 逐属性按字节复制字段值(浅拷贝)。
     * @param Dest 目标对象
     * @param Src 源对象
     *
     * @detail 实现是 memcpy(字段偏移, Size),只对 POD 字段布局正确;字段类型若
     *   含堆缓冲(MString/TVector 等)会共享内部指针,可能双重释放,使用前需确认
     *   目标类的属性都是平凡类型。Size 为 0 或取不到字段地址的属性会被跳过。
     */
    void CopyProperties(void* Dest, const void* Src) const;

    bool HasFlags(uint32 InFlags) const {
        return (ClassFlags & InFlags) != 0;
    }
    EClassKind GetKind() const {
        return ClassKind;
    }

    /**
     * @brief 设置构造/析构跳板。
     * @tparam TObject 具体类
     *
     * @detail 构造跳板按 InPlace 是否为空决定 placement new 还是普通 new;
     *   析构跳板用 delete 释放。因此 CreateInstance 与 Construct 必须与
     *   DestroyInstance 配对使用,不能与 new/delete 混用。
     */
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

    /**
     * @brief 设置类元数据。
     * @param InName 类名(同时用于派生 AssetTypeId)
     * @param InPath 路径信息(生成代码传 __FILE__)
     * @param InParent 反射父类;生成代码恒传 nullptr,父类由 SetParent 另行接线
     * @param InFlags 类标志位
     *
     * @detail AssetTypeId 在这里由类名一次性算出,改名即换 id。
     */
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
    /**
     * @brief 显式设置反射父类(SetMeta 之后调用)。
     * @param InParent 父类;可为空
     */
    void SetParent(MClass* InParent) {
        ParentClass = InParent;
    }

    void SetKind(EClassKind InKind) {
        ClassKind = InKind;
    }

    void SetCppTypeIndex(const std::type_index& InCppTypeIndex) {
        CppTypeIndex = InCppTypeIndex;
    }

    /**
     * @brief 注册一个属性并补齐它的运行期字段。
     * @param InProperty 待注册属性;为空直接忽略
     *
     * @detail 副作用依次为:
     *   1. PropertyId = ++GlobalPropertyId(全反射系统共享的单调计数器,仅进程内有效)
     *   2. AssetFieldId = ComputeStableAssetFieldId(类名, 属性名);与已有属性重复
     *      时 LOG_FATAL 并抛 std::runtime_error(asset field id 是资产 ABI)
     *   3. DomainFlags 由属性标志推导:Replicated/RepNotify/RepToClient → Replication,
     *      SaveGame/PersistentData → Persistence,Asset → Asset
     *   最后把所有权收进 Properties。
     */
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

        // 域由标志推导,不单独配置:这样"域"永远是属性标志的派生结果,不会两处不一致。
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

    /**
     * @brief 注册一个函数并确定它的 FunctionId。
     * @param InFunction 待注册函数;为空会导致解引用,调用方必须先判空
     *
     * @detail FunctionId 的确定顺序:已显式设置则保留;否则优先用
     *   ComputeStableClientFunctionId(ClientApiName),没有 ClientApiName 时回退到
     *   ComputeStableReflectId(类名, 函数名)。与已有函数撞 id 只 LOG_WARN 不失败
     *   (客户端/服务端两套命名空间可能正常重叠),最后设置 OwnerClass 并收编所有权。
     */
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
