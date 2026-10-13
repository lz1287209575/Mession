/**
 * @file Reflection.h
 * @brief 反射系统核心:反射对象基类 MObject、类/结构体/枚举全局注册表,以及生成器使用的标记宏。
 *
 * @detail 注册表全部是"函数内 static"而非全局变量,前提是 MObject 是 inline 静态成员,
 *   每个 TU 看到的都是同一份;对象表(GetObjectMap)以 ObjectId 为键,由构造函数写入、
 *   析构函数移除,因此任何 MObject 都必然在表中(析构期除外)。
 *   所有注册都发生在静态初始化/首次调用时,注册表本身不做线程同步 ——
 *   约定是启动阶段单线程完成注册,运行期只读查表。
 *   文件末尾包含两个 .inl:ReflectionArchiveInvoke.inl 定义 MReflectArchive 与调用适配,
 *   ReflectionPropertyTemplates.inl 定义属性模板与 JSON/二进制序列化。
 */
#pragma once

#include "Common/Runtime/Id.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Class.h"
#include "Common/Runtime/Reflect/Property.h"
#include "Common/Runtime/StringUtils.h"
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>

class MReflectArchive;
class MObject;
bool BuildServerRpcPayload(uint16 FunctionId, const TByteArray& InPayload, TByteArray& OutData);

/** @brief 函数/RPC 形参的存储类型:去掉引用与 cv 限定后的裸类型。 */
template <typename T> using TRpcArgStorage = std::remove_cv_t<std::remove_reference_t<T>>;

/** @brief 反射取值的存储类型:语义同 TRpcArgStorage,用于按值序列化的场合。 */
template <typename T> using TReflectStorage = std::remove_cv_t<std::remove_reference_t<T>>;

/**
 * @brief MObject - 反射对象的基类,提供类元数据、父子层级、脏标记与反射调用入口。
 *
 * @detail 生命周期与所有权:
 *   - 构造时把自己登记进全局对象表(ObjectId → this),析构时移除,因此不要移动构造。
 *   - 父对象通过 Children(TVector<TSharedPtr<MObject>>)持有子对象;加入 RootSet 的对象
 *     由全局 RootSet 持有一份 TSharedPtr,保证不被回收。
 *   - 拷贝/移动构造与赋值均已 delete,子类不能再打开这些口子。
 *   脏标记(DirtyPropertyIds / DirtyDomainFlags)记录"哪些字段被改过、属于哪些数据域",
 *   供复制与存档按域增量同步;写入字段后必须调用 MarkPropertyDirty 系列,否则不会同步。
 */
class MObject {
    private:
    /** @brief 内部对象标志位;对外只通过 IsRooted / IsDefaultSubObject 暴露。 */
    enum : uint64 {
        ObjectFlag_RootSet          = 1ull << 0,
        ObjectFlag_DefaultSubObject = 1ull << 1,
    };

    // 下面这一组 Get*Map() 都是"函数内 static"注册表,保证跨 TU 共享同一份实例;
    // 类/结构体/枚举各按 名字、数字 id、资产类型 id、C++ type_index 建四种索引,
    // 运行期只读,注册阶段由 RegisterClass / RegisterStruct / RegisterEnum 写入。
    inline static TMap<MString, MClass*>& GetClassMap() {
        static TMap<MString, MClass*> Map;
        return Map;
    }

    inline static TMap<uint16, MClass*>& GetClassIdMap() {
        static TMap<uint16, MClass*> Map;
        return Map;
    }

    inline static TMap<uint32, MClass*>& GetClassAssetTypeMap() {
        static TMap<uint32, MClass*> Map;
        return Map;
    }

    inline static TMap<MString, MEnum*>& GetEnumMap() {
        static TMap<MString, MEnum*> Map;
        return Map;
    }

    inline static TMap<uint16, MEnum*>& GetEnumIdMap() {
        static TMap<uint16, MEnum*> Map;
        return Map;
    }

    inline static TMap<std::type_index, MEnum*>& GetEnumTypeMap() {
        static TMap<std::type_index, MEnum*> Map;
        return Map;
    }

    inline static TMap<MString, MClass*>& GetStructMap() {
        static TMap<MString, MClass*> Map;
        return Map;
    }

    inline static TMap<uint16, MClass*>& GetStructIdMap() {
        static TMap<uint16, MClass*> Map;
        return Map;
    }

    inline static TMap<uint32, MClass*>& GetStructAssetTypeMap() {
        static TMap<uint32, MClass*> Map;
        return Map;
    }

    inline static TMap<std::type_index, MClass*>& GetStructTypeMap() {
        static TMap<std::type_index, MClass*> Map;
        return Map;
    }

    protected:
    uint64                       ObjectId    = 0;
    bool                         bReplicated = false;
    MClass*                      Class       = nullptr;
    uint64                       ObjectFlags = 0;
    MString                      Name;
    MObject*                     Outer = nullptr; // 非持有反向指针:所有权在 Outer->Children
    TVector<TSharedPtr<MObject>> Children;        // 本对象持有并负责释放的子对象
    uint64                       DirtyDomainFlags = ToMask(EPropertyDomainFlags::None);
    TSet<uint16>                 DirtyPropertyIds; // 按 PropertyId 去重的脏字段集合

    public:
    /**
     * @brief 构造:分配全局唯一 ObjectId 并把自己登记进全局对象表。
     *
     * @detail ObjectId 由 MUniqueIdGenerator 生成,只会自增不会复用;
     *   构造期就登记是为了让反射系统随时能由 id 反查对象。
     *   派生类不要在此阶段访问 Class 元数据,那时还可能是 nullptr。
     */
    MObject() : ObjectId(MUniqueIdGenerator::Generate()) {
        GetObjectMap()[ObjectId] = this;
    }
    MObject(const MObject&)            = delete;
    MObject& operator=(const MObject&) = delete;
    MObject(MObject&&)                 = delete;
    MObject& operator=(MObject&&)      = delete;
    virtual ~MObject();

    uint64 GetObjectId() const {
        return ObjectId;
    }
    /**
     * @brief SetObjectId - 改写对象 id,并同步搬到全局对象表的对应槽位。
     * @param InId 新的对象 id
     *
     * @detail 这不是单纯赋值:会先从对象表删掉旧键再插入新键,因此 id 在整个进程内
     *   始终唯一;id 相同则直接返回(避免无意义的表操作)。
     */
    void SetObjectId(uint64 InId) {
        if (ObjectId == InId) {
            return;
        }

        GetObjectMap().erase(ObjectId);
        ObjectId                 = InId;
        GetObjectMap()[ObjectId] = this;
    }
    void SetReplicated(bool bInReplicated) {
        bReplicated = bInReplicated;
    }
    bool IsReplicated() const {
        return bReplicated;
    }

    // 类与对象信息查询;GetClass() 是虚函数,派生类由 MGENERATED_BODY 覆盖成返回 StaticClass()
    virtual MClass* GetClass() const {
        return Class;
    }
    uint64 GetId() const {
        return GetObjectId();
    }
    const MString& GetName() const {
        return Name;
    }
    void SetName(const MString& InName) {
        Name = InName;
    }
    MObject* GetOuter() const {
        return Outer;
    }
    const TVector<TSharedPtr<MObject>>& GetChildren() const {
        return Children;
    }
    bool IsRooted() const {
        return (ObjectFlags & ObjectFlag_RootSet) != 0;
    }
    bool IsDefaultSubObject() const {
        return (ObjectFlags & ObjectFlag_DefaultSubObject) != 0;
    }

    // 生命周期虚函数:基类全部是空实现,派生类按需覆盖
    virtual void BeginPlay() {
    }
    virtual void Tick(float /*DeltaTime*/) {
    }
    virtual void Destroy() {
    }
    virtual MString ToString() const;
    // step-2: GetClientRouteTarget/GetClientResponseTarget 已随 ClientProtocol
    // 重构删除(原 RpcDispatch 的 message-type + RouteName + WrapMode + AuthMode
    // 整套 dispatch 都被换为 Manifest 反射)。Class.h 里 Reflection 字段
    // (MessageName/RouteName/TargetName/AuthMode/WrapMode/ClientParamBinder/
    // ClientCallHandler) 同步删除。
    virtual void VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const;

    // 按名字做字段读写:名字查不到或对象没有类元数据时静默失败(nullptr / 不写入)
    template <typename T> T* GetProperty(const MString& InName) const {
        if (!Class) {
            return nullptr;
        }
        auto* Prop = Class->FindProperty(InName);
        if (!Prop) {
            return nullptr;
        }
        return Prop->GetValuePtr<T>(const_cast<void*>(static_cast<const void*>(this)));
    }

    template <typename T> void SetProperty(const MString& InName, const T& Value) {
        if (!Class) {
            return;
        }
        auto* Prop = Class->FindProperty(InName);
        if (!Prop) {
            return;
        }
        *Prop->GetValuePtr<T>(this) = Value;
        MarkPropertyDirty(Prop); // 反射写入必须打脏标记,否则复制/存档看不到这次修改
    }

    bool CallFunction(const MString& InName);

    template <typename... TArgs> bool CallFunctionArgs(const MString& InName, TArgs&&... Args);

    template <typename TReturn, typename... TArgs> bool CallFunctionWithReturn(const MString& InName, TReturn& OutReturn, TArgs&&... Args);

    bool ProcessEvent(const MString& InName, void* Params);
    bool ProcessEvent(MFunction* Func, void* Params);
    bool InvokeSerializedFunction(MFunction* Func, MReflectArchive& InAr);

    // 类注册与查表:注册只在启动阶段发生,查表在运行期是只读的
    template <typename T> static MClass* StaticClass() {
        return T::StaticClass();
    }

    static MClass* FindClass(const MString& InName) {
        auto It = GetClassMap().find(InName);
        return (It != GetClassMap().end()) ? It->second : nullptr;
    }

    static MClass* FindClass(uint16 InId) {
        auto It = GetClassIdMap().find(InId);
        return (It != GetClassIdMap().end()) ? It->second : nullptr;
    }

    static MClass* FindClassByAssetTypeId(uint32 InAssetTypeId) {
        auto It = GetClassAssetTypeMap().find(InAssetTypeId);
        return (It != GetClassAssetTypeMap().end()) ? It->second : nullptr;
    }

    static TVector<MClass*> GetAllClasses() {
        TVector<MClass*> Result;
        Result.reserve(GetClassMap().size());
        for (const auto& Pair : GetClassMap()) {
            if (Pair.second) {
                Result.push_back(Pair.second);
            }
        }
        return Result;
    }

    /**
     * @brief RegisterClass - 把一个类的元数据登记进三张类索引表。
     * @param InClass 待登记的类元数据;为 nullptr 时直接返回
     *
     * @detail 资产类型 id 唯一属于资产 ABI:若同 id 已被别的类占用,先 LOG_FATAL
     *   再抛 std::runtime_error —— 这是不可恢复的配置错误,继续跑会读错资产。
     *   同一个类重复注册是允许的(ExistingClass == InClass 时跳过冲突检查)。
     */
    static void RegisterClass(MClass* InClass) {
        if (!InClass) {
            return;
        }

        if (MClass* ExistingClass = FindClassByAssetTypeId(InClass->GetAssetTypeId()); ExistingClass && ExistingClass != InClass) {
            const MString Message = "Reflection asset type id collision: class=" + InClass->GetName() + " existing=" + ExistingClass->GetName() + " asset_type_id=" + std::to_string(InClass->GetAssetTypeId());
            LOG_FATAL("%s", Message.c_str());
            throw std::runtime_error(Message);
        }

        GetClassMap()[InClass->GetName()]                 = InClass;
        GetClassIdMap()[InClass->GetId()]                 = InClass;
        GetClassAssetTypeMap()[InClass->GetAssetTypeId()] = InClass;
    }

    // 对象表按 ObjectId 直查;GetAllObjects 返回的是裸指针快照,不持有所有权
    static MObject* FindObject(uint64 InObjectId) {
        auto It = GetObjectMap().find(InObjectId);
        return (It != GetObjectMap().end()) ? It->second : nullptr;
    }

    static TVector<MObject*> GetAllObjects() {
        TVector<MObject*> Result;
        Result.reserve(GetObjectMap().size());
        for (const auto& Pair : GetObjectMap()) {
            if (Pair.second) {
                Result.push_back(Pair.second);
            }
        }
        return Result;
    }

    // 三个 FindEnum 重载按 名字/id/C++ type_index 查;都先确保枚举注册单元已被链接进来
    static MEnum* FindEnum(const MString& InName) {
        EnsureEnumsRegistered();
        auto It = GetEnumMap().find(InName);
        return (It != GetEnumMap().end()) ? It->second : nullptr;
    }

    static MEnum* FindEnum(uint16 InId) {
        EnsureEnumsRegistered();
        auto It = GetEnumIdMap().find(InId);
        return (It != GetEnumIdMap().end()) ? It->second : nullptr;
    }

    static MEnum* FindEnum(const std::type_index& InCppTypeIndex) {
        EnsureEnumsRegistered();
        auto It = GetEnumTypeMap().find(InCppTypeIndex);
        return (It != GetEnumTypeMap().end()) ? It->second : nullptr;
    }

    /**
     * @brief EnsureEnumsRegistered - 惰性触发一次"注册全部枚举"的引导函数。
     *
     * @detail MHeaderTool_RegisterAllEnums() 由生成器在
     *   MEnumRegistration.mgenerated.cpp 中定义,内部全是匿名命名空间的静态对象。
     *   如果没人引用它,静态库链接时整个 TU 会被链接器丢弃(静态对象不会被拉取),
     *   枚举就查不到;在函数体里 extern 声明并调用,正好制造一个必须解析的符号引用,
     *   从而把注册单元拉进来。static 局部变量保证只执行一次,重入/多线程调用只会有
     *   一次真正注册(其余线程可能同时进入,但重跑注册是幂等的)。
     */
    static void EnsureEnumsRegistered() {
        static bool bDone = false;
        if (bDone) {
            return;
        }
        bDone = true;
        extern void MHeaderTool_RegisterAllEnums();
        MHeaderTool_RegisterAllEnums();
    }

    /**
     * @brief RegisterEnum - 登记枚举元数据。
     * @param InEnum 待登记的枚举元数据;为 nullptr 时直接返回
     *
     * @detail type_index 为 typeid(void) 表示该枚举没有登记 C++ 类型(例如纯脚本侧
     *   枚举),此时不写入 type_index 索引 —— 否则所有无类型枚举会互相覆盖。
     */
    static void RegisterEnum(MEnum* InEnum) {
        if (!InEnum) {
            return;
        }

        GetEnumMap()[InEnum->GetName()] = InEnum;
        GetEnumIdMap()[InEnum->GetId()] = InEnum;
        if (InEnum->GetCppTypeIndex() != std::type_index(typeid(void))) {
            GetEnumTypeMap()[InEnum->GetCppTypeIndex()] = InEnum;
        }
    }

    // 结构体(含 MSTRUCT 标注的 POD)查表;按 名字/id/资产类型 id/C++ type_index
    static MClass* FindStruct(const MString& InName) {
        auto It = GetStructMap().find(InName);
        return (It != GetStructMap().end()) ? It->second : nullptr;
    }

    static MClass* FindStruct(uint16 InId) {
        auto It = GetStructIdMap().find(InId);
        return (It != GetStructIdMap().end()) ? It->second : nullptr;
    }

    static MClass* FindStructByAssetTypeId(uint32 InAssetTypeId) {
        auto It = GetStructAssetTypeMap().find(InAssetTypeId);
        return (It != GetStructAssetTypeMap().end()) ? It->second : nullptr;
    }

    static MClass* FindStruct(const std::type_index& InCppTypeIndex) {
        auto It = GetStructTypeMap().find(InCppTypeIndex);
        return (It != GetStructTypeMap().end()) ? It->second : nullptr;
    }

    /**
     * @brief RegisterStruct - 把一个结构体的元数据登记进四张结构体索引表。
     * @param InStruct 待登记的结构体元数据;为 nullptr 时直接返回
     *
     * @detail 与 RegisterClass 同样的资产类型 id 唯一性约束(碰撞即 LOG_FATAL + 抛异常)。
     *   与类不同的是这里额外登记 C++ type_index 索引:反射要靠它把
     *   `typeid(T)` 映射到结构体元数据,从而决定"按字段逐个序列化"还是"按字节拷贝"。
     *   同样跳过 typeid(void),避免无类型条目互相覆盖。
     */
    static void RegisterStruct(MClass* InStruct) {
        if (!InStruct) {
            return;
        }

        if (MClass* ExistingStruct = FindStructByAssetTypeId(InStruct->GetAssetTypeId()); ExistingStruct && ExistingStruct != InStruct) {
            const MString Message = "Reflection asset type id collision: struct=" + InStruct->GetName() + " existing=" + ExistingStruct->GetName() + " asset_type_id=" + std::to_string(InStruct->GetAssetTypeId());
            LOG_FATAL("%s", Message.c_str());
            throw std::runtime_error(Message);
        }

        GetStructMap()[InStruct->GetName()]                 = InStruct;
        GetStructIdMap()[InStruct->GetId()]                 = InStruct;
        GetStructAssetTypeMap()[InStruct->GetAssetTypeId()] = InStruct;
        if (InStruct->GetCppTypeIndex() != std::type_index(typeid(void))) {
            GetStructTypeMap()[InStruct->GetCppTypeIndex()] = InStruct;
        }
    }

    /** @brief SetClass - 由反射系统在创建实例后回填类元数据。 */
    void SetClass(MClass* InClass) {
        Class = InClass;
    }

    /**
     * @brief SetOuter - 改父对象,并同步维护新旧父的 Children 列表。
     * @param InOuter 新父对象;可传 nullptr(脱离父对象)
     *
     * @detail 实现见 Reflection.cpp:它是 Children 与 Outer 的唯一同步点,
     *   业务侧不要绕开它直接改 Outer。
     */
    void SetOuter(MObject* InOuter);

    /**
     * @brief AddToRootSet - 把对象加入全局 Root 集,使其不被回收。
     * @param Object 要加入的对象;无效(空)时直接返回
     *
     * @detail 这是"公共接口",但调用点集中在 NewMObject 内部:Root 集持有一份
     *   TSharedPtr,是对象存活到进程退出的依据。同时置 Root 标志位。
     */
    static void AddToRootSet(const TSharedPtr<MObject>& Object);
    /**
     * @brief AddToRoot - 置 Root 标志并把已存在的 TSharedPtr 引用插入 Root 集。
     *
     * @detail 只会插入"能在 Children 里找到的"引用;若对象从未被 MakeShared 持有,
     *   这里找不到引用,就只是置了标志位。新代码优先用 AddToRootSet。
     */
    void AddToRoot();
    /** @brief RemoveFromRoot - 清 Root 标志并从 Root 集移除引用(少一次 Root 持有)。 */
    void RemoveFromRoot();
    void MarkAsDefaultSubObject() {
        AddObjectFlags(ObjectFlag_DefaultSubObject);
    }
    void SetObjectFlags(uint64 InFlags) {
        ObjectFlags = InFlags;
    }
    void AddObjectFlags(uint64 InFlags) {
        ObjectFlags |= InFlags;
    }
    void RemoveObjectFlags(uint64 InFlags) {
        ObjectFlags &= ~InFlags;
    }
    uint64 GetObjectFlags() const {
        return ObjectFlags;
    }

    // 脏标记:按"字段 id 集合 + 域位图"两份数据维护,缺一不可。
    // 用 PropertyId 而非字段名,避免每次比较都做字符串查找;
    // 域位图是 PropertyId 集合的缓存,供 HasDirtyDomain 做 O(1) 快速判断。
    void MarkPropertyDirty(const MString& InName) {
        if (!Class) {
            return;
        }

        MProperty* Prop = Class->FindProperty(InName);
        MarkPropertyDirty(Prop);
    }

    void MarkPropertyDirtyById(uint16 InPropertyId) {
        if (!Class) {
            return;
        }

        MProperty* Prop = Class->FindPropertyById(InPropertyId);
        MarkPropertyDirty(Prop);
    }

    void SetPropertyDirtyById(uint16 InPropertyId) {
        MarkPropertyDirtyById(InPropertyId);
    }

    /**
     * @brief MarkPropertyDirty - 把字段记入脏集合,并把它的域并入脏域位图。
     * @param InProperty 变脏的字段描述符;为 nullptr 时直接返回
     *
     * @detail 传入 nullptr(名字或 id 查不到)会被静默忽略 —— 也就是说字段名写错
     *   不会报错,只是这次修改不会被同步,排查时要留意。
     */
    void MarkPropertyDirty(const MProperty* InProperty) {
        if (!InProperty) {
            return;
        }

        DirtyPropertyIds.insert(InProperty->PropertyId);
        DirtyDomainFlags |= InProperty->DomainFlags;
    }

    void SetPropertyDirty(const MProperty* InProperty) {
        MarkPropertyDirty(InProperty);
    }

    bool HasDirtyDomain(EPropertyDomainFlags InDomain) const {
        return HasAnyPropertyDomains(DirtyDomainFlags, InDomain);
    }

    bool HasAnyDirtyPropertyForDomain(EPropertyDomainFlags InDomain) const {
        if (!Class || DirtyPropertyIds.empty()) {
            return false;
        }

        for (uint16 PropertyId : DirtyPropertyIds) {
            if (const MProperty* Prop = Class->FindPropertyById(PropertyId)) {
                if (Prop->HasAnyDomains(InDomain)) {
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * @brief ClearDirtyDomain - 某个数据域同步完成后,清除该域的脏状态。
     * @param InDomain 已同步完毕的域
     *
     * @detail 只有"该字段只属于这一个域"时才能把字段从脏集合里摘掉;
     *   若字段同时属于多个域,而其他域还没同步,则必须保留。
     *   字段集合修改完后,域位图按剩余字段重新计算(而不是简单清位),
     *   否则会丢掉那些仍为脏的其他域。没有类元数据时退化为直接清位。
     */
    void ClearDirtyDomain(EPropertyDomainFlags InDomain) {
        if (!Class) {
            DirtyDomainFlags &= ~ToMask(InDomain);
            return;
        }

        for (auto It = DirtyPropertyIds.begin(); It != DirtyPropertyIds.end();) {
            const MProperty* Prop = Class->FindPropertyById(*It);
            if (!Prop || Prop->HasAnyDomains(InDomain)) { // 元数据缺失也一并清掉,避免脏集合无限增长
                It = DirtyPropertyIds.erase(It);
            } else {
                ++It;
            }
        }

        DirtyDomainFlags = ToMask(EPropertyDomainFlags::None);
        for (uint16 PropertyId : DirtyPropertyIds) {
            if (const MProperty* Prop = Class->FindPropertyById(PropertyId)) {
                DirtyDomainFlags |= Prop->DomainFlags;
            }
        }
    }

    private:
    static TMap<uint64, MObject*>&    GetObjectMap();
    static TSet<TSharedPtr<MObject>>& GetRootSet();
    void                              AddChildObject(const TSharedPtr<MObject>& Child);
    void                              RemoveChildObject(const TSharedPtr<MObject>& Child);

    /**
     * @brief FindChildShared - 从自己的 Children 里找出指向 Child 的那个 TSharedPtr 引用。
     * @param Child 目标对象;为 nullptr 时返回空
     * @return 找到的引用;没有则返回空 TSharedPtr
     *
     * @detail 返回的是"已有引用"而不是新建的:调用方需要的是同一个控制块,
     *   这样才能把它从 Root 集里精确摘除。
     */
    TSharedPtr<MObject> FindChildShared(MObject* Child) const;
    /** @brief FindRootShared - 在全局 Root 集中找出指向 Object 的 TSharedPtr 引用;找不到返回空。 */
    TSharedPtr<MObject> FindRootShared(MObject* Object) const;

    /** @brief MarkAsRootSet - 仅置 Root 标志位,不碰 Root 集(NewMObject 内部使用)。 */
    void MarkAsRootSet() {
        ObjectFlags |= ObjectFlag_RootSet;
    }

    template <typename TReturn, typename... TArgs> bool InvokeFunction(const MString& InName, TReturn* OutReturn, TArgs&&... Args);

    bool InvokeFunction(MFunction* Func, MReflectArchive* InAr, MReflectArchive* OutAr);
};

/**
 * @brief MCLASS/MSTRUCT/MENUM/MPROPERTY/MFUNCTION - 纯标记宏,展开为空。
 *
 * @detail 这些宏不产生任何代码,只是给 MHeaderTool 的 AST 扫描器提供"这里是反射
 *   声明"的锚点;登记工作全部由生成器输出的 MREGISTER_* 代码完成。
 *   手动写这些标记但没跑生成器,等于什么都没注册。
 */
#define MCLASS(...)
#define MSTRUCT(...)
#define MENUM(...)
#define MPROPERTY(...)
#define MFUNCTION(...)
#define GENERATED_BODY() /* MGENERATED_BODY 的兼容别名,同样不展开 */
#define __MFUNC__(...)   /* MHeaderTool 的源码标记,展开为空 */
/** @brief MFUNC_OBJECT - MHeaderTool 生成代码里实际使用的运行期类名。 */
#define MFUNC_OBJECT MFunction

/**
 * @brief PROPERTY - 生成一个内联 MProperty 派生结构体,并把实例作为成员嵌进类里。
 * @param Type  字段的 EPropertyType 枚举名(不带前缀,宏里会拼上 EPropertyType::)
 * @param Name  字段名;宏会字符串化成属性名,并要求类里存在同名成员
 * @param Flags EPropertyFlags 枚举名(不带前缀)
 *
 * @detail 只能在 MGENERATED_BODY 作用域内使用(依赖 ThisClass 别名);
 *   字段地址用 offsetof 计算,所以 Name 必须是标准布局成员。
 *   注意这是"手工注册"路径,与生成器输出的 MREGISTER_PROPERTY 并存。
 */
#define PROPERTY(Type, Name, Flags)                                                                                                          \
    struct SProperty_##Name : public MProperty {                                                                                             \
        SProperty_##Name() : MProperty(#Name, EPropertyType::Type, offsetof(ThisClass, Name), sizeof(Type), std::type_index(typeid(Type))) { \
            this->Flags = EPropertyFlags::Flags;                                                                                             \
        }                                                                                                                                    \
    } Name##_Property;

/**
 * @brief FUNCTION - 生成一个 MFunction 派生结构体类型,仅用于声明(不含调用体)。
 * @param Name  函数名;字符串化为 MFunction::Name
 * @param Flags EFunctionFlags 枚举名(不带前缀)
 *
 * @detail 只定义一个内部结构体类型,不产生实例,也没有把函数登记进类 ——
 *   真正登记要调用 MREGISTER_FUNCTION。旧代码保留此宏是为了标记用途。
 */
#define FUNCTION(Name, Flags)                    \
    struct MFunction_##Name : public MFunction { \
        MFunction_##Name() {                     \
            Name  = #Name;                       \
            Flags = EFunctionFlags::Flags;       \
        }                                        \
    };

/**
 * @brief MGENERATED_BODY - 写在类内部,声明反射所需的别名与注册入口。
 * @param ClassName   本类名,成为 ThisClass 别名
 * @param ParentClass 父类名,成为 Super 别名
 * @param Flags       类级 EClassFlags(此处只透传给 MIMPLEMENT_CLASS 使用)
 *
 * @detail 生成 static StaticClass() 声明、覆盖 GetClass(),并私下声明
 *   RegisterAllProperties/RegisterAllFunctions —— 这两个静态函数由生成器在 .cpp 中实现。
 *   注意它把访问级别切成 public→private,写在类中间的反射成员要留意可见性。
 */
#define MGENERATED_BODY(ClassName, ParentClass, Flags)  \
    public:                                             \
    using ThisClass = ClassName;                        \
    using Super     = ParentClass;                      \
    static MClass*  StaticClass();                      \
    virtual MClass* GetClass() const override {         \
        return StaticClass();                           \
    }                                                   \
                                                        \
    private:                                            \
    static void RegisterAllProperties(MClass* InClass); \
    static void RegisterAllFunctions(MClass* InClass);

/**
 * @brief MIMPLEMENT_CLASS - 在类外实现 StaticClass():一次性建元数据并完成注册。
 * @param ClassName   类名
 * @param ParentClass 父类名(保留形参,当前实现未使用)
 * @param Flags       类级 EClassFlags,透传给 MClass::SetMeta
 *
 * @detail 用函数内 static 保证"首次调用时构建一次",构建期间依次设置元数据、
 *   构造工厂、注册属性与函数,最后登记进全局注册表;之后调用直接返回缓存指针。
 *   因此本宏不是线程安全的,约定在启动阶段首次调用。
 */
#define MIMPLEMENT_CLASS(ClassName, ParentClass, Flags)           \
    MClass* ClassName::StaticClass() {                            \
        static MClass* Class = nullptr;                           \
        if (!Class) {                                             \
            Class = new MClass();                                 \
            Class->SetMeta(#ClassName, __FILE__, nullptr, Flags); \
            Class->SetConstructor<ClassName>();                   \
            ClassName::RegisterAllProperties(Class);              \
            ClassName::RegisterAllFunctions(Class);               \
            MObject::RegisterClass(Class);                        \
        }                                                         \
        return Class;                                             \
    }

/**
 * @brief MREGISTER_PROPERTY - 用成员指针方式注册一个字段属性。
 * @param CppType   字段的底层 C++ 类型(用于取地址与 typeid)
 * @param PropEnum  EPropertyType 枚举名(不带前缀)
 * @param PropName  字段名
 * @param PropFlags EPropertyFlags 位组合
 *
 * @detail 走 TMemberProperty:靠成员指针取地址,不依赖 offsetof,因此对非标准布局也成立。
 *   必须在 RegisterAllProperties 里调用(依赖 ThisClass 与 InClass)。
 */
#define MREGISTER_PROPERTY(CppType, PropEnum, PropName, PropFlags)                                                                 \
    do {                                                                                                                           \
        auto* Prop = new TMemberProperty<ThisClass, CppType, &ThisClass::PropName>(#PropName, EPropertyType::PropEnum, PropFlags); \
        InClass->RegisterProperty(Prop);                                                                                           \
    } while (0)

/**
 * @brief MREGISTER_FUNCTION - 把一个无参函数名登记进类的函数表。
 * @param FuncName  函数名;字符串化后作为 MFunction::Name
 * @param FuncFlags EFunctionFlags 位组合
 *
 * @detail 只带名字与标志,不绑定调用体(NativeInvoke 为空),因此登记后无法通过
 *   InvokeFunction 真正调用;需要可调用的函数请用 MREGISTER_NATIVE_METHOD
 *   或 MREGISTER_RPC_FUNCTION 系列。
 */
#define MREGISTER_FUNCTION(FuncName, FuncFlags)  \
    do {                                         \
        auto* Func  = new MFunction();           \
        Func->Name  = #FuncName;                 \
        Func->Flags = EFunctionFlags::FuncFlags; \
        InClass->RegisterFunction(Func);         \
    } while (0)

// ---- 函数/方法注册入口 ----
// MREGISTER_NATIVE_METHOD* 注册普通成员函数:只绑定调用体,不带网络语义。
// MREGISTER_RPC_* 额外记录 RpcType/bReliable/EndpointServerType,由网络侧据此决定投递方式。
// 全部都要在 RegisterAllFunctions 里调用;Flags/RpcKind/EndpointType 传的是"不带前缀的枚举名",
// 宏内部负责拼上 EFunctionFlags:: / ERpcType:: / EServerType::。

#define MREGISTER_NATIVE_METHOD_0(MethodName, FuncFlags) MREGISTER_NATIVE_METHOD(MethodName, FuncFlags)

#define MREGISTER_NATIVE_METHOD(MethodName, FuncFlags)                                                   \
    do {                                                                                                 \
        InClass->RegisterFunction(CreateNativeFunction<&ThisClass::MethodName>(#MethodName, FuncFlags)); \
    } while (0)

#define MREGISTER_RPC_FUNCTION(MethodPtr, FuncNameLiteral, FuncFlags, RpcKind, ReliableValue)                                                  \
    do {                                                                                                                                       \
        InClass->RegisterFunction(CreateRpcFunction<MethodPtr>(FuncNameLiteral, EFunctionFlags::FuncFlags, ERpcType::RpcKind, ReliableValue)); \
    } while (0)

// 带校验版本的 RPC 注册:ValidatePtr 是 `bool (TObject::*)(Args...) const` 成员函数指针,
// 反序列化出实参后先调用它;返回 false 则丢弃本次调用并打 LOG_WARN,不会真正执行方法体。
#define MREGISTER_RPC_FUNCTION_WITH_VALIDATE(MethodPtr, ValidatePtr, FuncNameLiteral, FuncFlags, RpcKind, ReliableValue)                                    \
    do {                                                                                                                                                    \
        InClass->RegisterFunction(CreateRpcFunction<MethodPtr, ValidatePtr>(FuncNameLiteral, EFunctionFlags::FuncFlags, ERpcType::RpcKind, ReliableValue)); \
    } while (0)

#define MREGISTER_RPC_FUNCTION_FOR_SERVER(MethodPtr, FuncNameLiteral, FuncFlags, RpcKind, ReliableValue, EndpointType)                                                    \
    do {                                                                                                                                                                  \
        InClass->RegisterFunction(CreateRpcFunction<MethodPtr>(FuncNameLiteral, EFunctionFlags::FuncFlags, ERpcType::RpcKind, ReliableValue, EServerType::EndpointType)); \
    } while (0)

#define MREGISTER_RPC_METHOD(MethodName, FuncFlags, RpcKind, ReliableValue) MREGISTER_RPC_FUNCTION(&ThisClass::MethodName, #MethodName, FuncFlags, RpcKind, ReliableValue)

#define MREGISTER_RPC_METHOD_FOR_SERVER(MethodName, FuncFlags, RpcKind, ReliableValue, EndpointType) MREGISTER_RPC_FUNCTION_FOR_SERVER(&ThisClass::MethodName, #MethodName, FuncFlags, RpcKind, ReliableValue, EndpointType)

#define MREGISTER_RPC_METHOD_WITH_VALIDATE(MethodName, ValidateName, FuncFlags, RpcKind, ReliableValue) MREGISTER_RPC_FUNCTION_WITH_VALIDATE(&ThisClass::MethodName, &ThisClass::ValidateName, #MethodName, FuncFlags, RpcKind, ReliableValue)

/**
 * @brief MDECLARE_RPC_METHOD / MREGISTER_RPC_METHOD_ENTRY - 声明 + 注册的"成对"宏,
 *   供 MHeaderTool 扫描的老式 RPC 入口使用。
 * @param MethodName    方法名(声明宏会原样拼成 `void MethodName Signature`)
 * @param Signature     形参表,如 `(int32 A, const MString& B)`
 * @param FuncFlags     EFunctionFlags 枚举名
 * @param RpcKind       ERpcType 枚举名
 * @param ReliableValue 是否可靠(直接透传,bool)
 *
 * @detail 声明宏用 MFUNCTION 标记(展开为空);注册宏按方法名走
 *   MREGISTER_RPC_METHOD,即按名字重新取一次 &ThisClass::MethodName,
 *   因此方法必须是本类的非重载成员,否则会把后面那个宏参数丢掉。
 */
#define MDECLARE_RPC_METHOD(MethodName, Signature, FuncFlags, RpcKind, ReliableValue) MFUNCTION(FuncFlags) void MethodName Signature;

#define MREGISTER_RPC_METHOD_ENTRY(MethodName, Signature, FuncFlags, RpcKind, ReliableValue) MREGISTER_RPC_METHOD(MethodName, FuncFlags, RpcKind, ReliableValue);

// 下面两对宏是为了兼容老写法:ApiName 形参被接收但完全不使用(历史上有独立的
// handler 名字,现在登记只认方法名),保留形参位置以免旧调用点编译失败。
#define MDECLARE_RPC_METHOD_WITH_HANDLER(MethodName, ApiName, Signature, FuncFlags, RpcKind, ReliableValue) MDECLARE_RPC_METHOD(MethodName, Signature, FuncFlags, RpcKind, ReliableValue)

#define MREGISTER_RPC_METHOD_WITH_HANDLER_ENTRY(MethodName, ApiName, Signature, FuncFlags, RpcKind, ReliableValue) MREGISTER_RPC_METHOD_ENTRY(MethodName, Signature, FuncFlags, RpcKind, ReliableValue)

// 服务器承载型 RPC:ClassNameLiteral 只作可读标记、不参与展开(用 ThisClass 取地址),
// EndpointType 会转为 EServerType::EndpointType 记录到 MFunction::EndpointServerType。
#define MDECLARE_SERVER_HOSTED_RPC_METHOD(ClassNameLiteral, EndpointType, MethodName, Signature, FuncFlags, RpcKind, ReliableValue) MFUNCTION(FuncFlags) void MethodName Signature;

#define MREGISTER_SERVER_HOSTED_RPC_METHOD_ENTRY(ClassNameLiteral, EndpointType, MethodName, Signature, FuncFlags, RpcKind, ReliableValue) MREGISTER_RPC_METHOD_FOR_SERVER(MethodName, FuncFlags, RpcKind, ReliableValue, EndpointType);

/**
 * @brief GET_PROPERTY / SET_PROPERTY - 按字段名做带类型的读 / 写语法糖。
 * @param Object 目标 MObject 指针(会被字符串化成字段名查表)
 * @param Type   字段的 C++ 类型
 * @param Name   字段名(宏内字符串化)
 *
 * @detail 查不到字段时 GET 返回 nullptr、SET 静默不写入(见 MObject::GetProperty /
 *   SetProperty);SET 会顺带打脏标记。
 */
#define GET_PROPERTY(Object, Type, Name) (Object)->GetProperty<Type>(#Name)

#define SET_PROPERTY(Object, Type, Name, Value) (Object)->SetProperty<Type>(#Name, Value)

#include "Common/Runtime/Reflect/ReflectionArchiveInvoke.inl"
#include "Common/Runtime/Reflect/ReflectionPropertyTemplates.inl"
