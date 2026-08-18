# Actor / ActorMember 框架设计(F0 细化)

> 状态:设计细化,待评审。
> 上游:`2026-08-14-player-design.md` §3(目标架构:协议下沉到 ActorMember)。
> 范围:F0 框架演进——类型系统 + MHeaderTool 生成 + 运行时,为 F1(登录/背包成员验证)铺路。

## 1. 目标

让业务协议以 `MCLASS(Type=ActorMember)` 类声明,协议下沉到成员:

```cpp
MCLASS(Type = ActorMember)
class MPlayerItemContainer : public IActorMember
{
    MFUNCTION(ServerCall, Async)
    SFutureResult<FPlayerUseItemResponse> UseItem(const FPlayerUseItemRequest& InRequest);
};
```

Service 类不再声明任何业务协议;进程接收由框架内部机制完成。

## 2. 类型系统扩展

### 2.1 EClassKind(Class.h)
```cpp
enum class EClassKind : uint8 {
    Object = 0, Server = 1, Service = 2, Rpc = 3, Struct = 4,
    Actor = 5,        // 新增:Type=Actor(当前 CodeGenerator 未映射,半支持)
    ActorMember = 6,  // 新增:Type=ActorMember
};
```
- MHeaderTool `CodeGenerator.h:969` 加 `"Actor"` / `"ActorMember"` 映射分支。

### 2.2 EServerType(ServerConnection.h)
- 新增 `Player = 8`(MPlayerService 进程类型)。
- LoginService 复用遗留 `Login = 2`(枚举仍在;不恢复旧登录业务树,只作认证服务类型)。

## 3. MHeaderTool 生成设计

### 3.1 Actor 类生成(正式化 Type=Actor)
- 沿用 `MGENERATED_BODY`(StaticClass/GetClass/RegisterAllProperties/RegisterAllFunctions)。
- **Actor 需 MObject 身份**(`IActor + MObject`,仿 MRankListActor 先例)——成员是
  Component 子对象,`CreateDefaultSubObject<T>(Owner)` 要求 Owner 为 `MObject*`。
- 新增生成:**actor 注册表条目** `{ ClassName }`。

**actor ↔ 成员关联(已决策:反射扫描 + Meta 绑定)**:
- 成员类声明绑定目标:`MCLASS(Type=ActorMember, Meta=(Actor=MPlayerActor))`。
- 反射扫描:遍历 ClassRegistry 中 `EClassKind::ActorMember` 的类,按 `Meta.Actor`
  分组;创建某 actor 类型时**只挂载绑定到它的成员**(PoC 单 actor 类型=全部挂载的特例)。

### 3.2 ActorMember 类生成
- `MGENERATED_BODY`(成员是反射类,与 Service 类同机制)。
- 新增生成 **member 方法 FunctionId 注册表**(全局静态,类似 GClientManifestEntries):
  ```cpp
  // MPlayerItemContainer.mgenerated.cpp
  struct SMemberRpcEntry { uint16 FunctionId; const char* MemberClass; const char* MethodName; };
  const SMemberRpcEntry GMemberRpcEntries[] = {
      { 24601, "MPlayerItemContainer", "UseItem" },   // FunctionId = 稳定 id(member 类+方法)
  };
  ```
- FunctionId 计算(已决策 D1-a):`ComputeStableReflectId(MemberClass, MethodName)`——
  (member 类名, 方法名) 域,与现有 ServerCall 的 (类,方法) 域一致;不同 member 类同名方法不撞。

### 3.3 manifest 扩展(Gateway 路由)
- MClientManifest 条目:`OwnerType = member 类名`, **`TargetName = 所属服务`**(SEntry.TargetName
  字段已有)。
- **所属服务(已决策 D2-a,修订:从 Actor 推导,不逐 member 手写)**:
  - member 类只声明绑定:`Meta=(Actor=MPlayerActor)`;
  - **actor 类上声明一次** `Meta=(Service=MPlayerService)`(actor 决定运行在哪个服务进程);
  - 生成器:member → `Meta.Actor` → 查 actor 类的 `Meta.Service` → 推导 manifest `TargetName`
    (AST 已解析所有类 Meta,同 `ExtractMacroValue` 机制)。
- Gateway 按 Target 路由到对应服务进程(与现有 Echo 转发同机制)。

## 4. 运行时设计

### 4.1 IActorMember(成员基类,含反射身份,双宿主)
```cpp
namespace mession::actor {
// 宿主抽象:actor 容器(per-player)或服务实例(单例,如 LoginService)
class IMemberHost
{
public:
    virtual ~IMemberHost() = default;
    virtual IActorMember* FindMember(const MString& InTypeName) = 0;   // 成员表查询
    virtual void AddMember(const MString& InTypeName, TSharedPtr<IActorMember> InMember) = 0;
};

class IActorMember : public MObject   // 反射身份:GetClass/FindFunction/NewMObject 全可用
{
public:
    virtual ~IActorMember() = default;
    void AttachToHost(IMemberHost* InHost);   // OnAttach(挂宿主 + 注册进宿主成员表)
    void DetachFromHost();                     // OnDetach
    IMemberHost* GetHost() const { return Host; }

    // 成员间查找(同宿主):经宿主的成员表
    template<typename T> T* GetActorMember()
    {
        return Host ? static_cast<T*>(Host->FindMember(T::StaticClass()->GetName())) : nullptr;
    }
protected:
    IMemberHost* Host = nullptr;
};
}
```
- **决策:IActorMember 继承 MObject**——复用现有反射调用链
  (`MFunctionObject::NativeInvoker(MObject*, MReflectArchive*, MReflectArchive*)`),
  用户代码 `: public IActorMember` 写法不变。
- **双宿主**:per-player 成员挂 actor 容器(IActor 实现 IMemberHost);单例成员
  (认证 MLoginAuth)挂服务实例(MLoginService 实现 IMemberHost)——Login 侧无 actor 容器(已决策)。

### 4.2 成员表与 GetActorMember(宿主实现,Component 模型)
- `IMemberHost` 实现方维护成员表:`TMap<MString 类型名, TSharedPtr<IActorMember>>`。
- **成员 = Component 默认子对象(UE 风格)**:actor(IActor+MObject)实例化时,按 §3.1
  的 Meta 绑定扫描结果,用 `CreateDefaultSubObject<T>(this)` 创建并 `AttachToHost`
  (`Object.h` 已有该工厂:NewMObject + MarkAsDefaultSubObject,生命周期跟随 Owner)。
- 服务实例(MLoginService 等)同样实现 `IMemberHost`:Init 时创建单例成员并挂载。

### 4.3 进程内部成员分发器(框架内部,非业务信封;MNetServerBase 内置能力)

- **分发入口 = `MNetServerBase` 内置能力(已决策 F1-a + F2-b)**:业务进程统一骨架
  (监听/事件循环/Registry/async 投递)之上,再增加 member 调用分发——所有业务服务
  (`MPlayerService`/`MLoginService`/未来任意)继承即获得,**业务类零改动、零业务协议**:
  ```cpp
  // MNetServerBase(Common/Net/NetServerBase.h)
  MFUNCTION(ServerCall, Async)
  SFutureResult<TByteArray> FrameworkMemberDispatch(
      uint16 FunctionId, const TByteArray& Payload);   // 协议级入口,框架内部实现
  ```
- **`GMemberRpcEntries` 由 MHeaderTool 生成,零手动声明**(与 `GClientManifestEntries`
  同机制):扫描 `Type=ActorMember` 类的 `MFUNCTION(ServerCall)` → 算
  `ComputeStableReflectId(member 类, 方法)` → 生成静态表;业务只写成员类 + 协议方法。
- 处理流程(双路由):
  ```
  收到 (FunctionId, Payload)
  1. 查 GMemberRpcEntries → (MemberClass, MethodName)
  2. 反序列化请求(MReflectArchive,含继承字段)——细节 A:反序列化后取 PlayerId
  3. 定位宿主:
     - 请求含 PlayerId → MActorSystem::Find(PlayerId) → actor 宿主(per-player 成员)
     - 无 PlayerId(认证/服务级,如 Login)→ 服务实例宿主(MLoginService 成员表)
  4. 宿主成员表 → MemberClass 实例
  5. 反射调用:MemberClass::FindFunction(MethodName) → NativeInvoker(二进制进出参)
     (请求体已反序列化,复用不二次解析)
  6. 序列化响应 → 回包
  ```
- Gateway 侧:manifest 只路由到**进程级**(member 条目 Target=服务),进程内按 FunctionId
  进基类入口分发——Gateway 不解析业务请求、无绑定表依赖,保持薄。

## 5. 请求寻址:PlayerId 显式携带(已决策,2026-08 二次修订)

- **决策**:PlayerId 显式携带在业务请求里(用户确认)。请求继承基类:
  ```cpp
  MSTRUCT() struct FPlayerRequestBase { MPROPERTY() uint64 PlayerId; };
  MSTRUCT() struct FPlayerUseItemRequest : FPlayerRequestBase { MPROPERTY() uint64 ItemId; MPROPERTY() int32 Count; };
  ```
- **前置条件(F0 实现项):反射序列化必须支持继承**——当前 `MClass::WriteSnapshot` 只遍历
  本类 `Properties`、不递归父类;且生成器 `SetMeta(..., nullptr)` 未设 `ParentClass`
  (`SetParent` 零调用)。不修则基类字段(PlayerId)不落盘,反序列化往返不一致。
  **修复**:① 生成器对 MSTRUCT/MCLASS 有父类时 `SetParent(父类 StaticClass())`(AST 已解析
  `ParentClass`,Class.cpp/Class.h 机制已预留)② `WriteSnapshot`/`WriteSnapshotByDomain`/
  读取端**先父后子**递归 ③ round-trip 测试;现有继承链父类无 `MPROPERTY`,不改变现有行为。
- 分发器:反序列化请求(含继承字段)后 `FindProperty("PlayerId")` 取路由目标,
  请求体供 member 反射调用复用(不二次反序列化)。
- **服务级成员(认证 MLoginAuth 等,宿主=服务实例)**:请求无 PlayerId,FunctionId
  直发服务进程 → 服务实例成员表定位(§4.3 双路由第二支)。
- Global actor(排行榜/邮件):固定 ActorId 寻址(框架按 FunctionId 已知)。

## 6. 序列化(复用,不新造)

| 场景 | 机制 |
|---|---|
| 客户端 ↔ 服务进程 | MFUNCTION 现有生成(MReflectArchive 二进制) |
| member 方法调用 | 复用 `NativeInvoker`(MReflectArchive 进出参) |
| member 间调用 | 同 actor 同线程:直接调用 + `TAwaitable` 表达式 await(`TAwaitable<F>(args)`,F=函数名;成员方法 await 为 F0 演进项) |

**成员间 await 的 target 解析(已决策:隐式,不手传)**——成员方法内 `TAwaitable<T::Method>(args...)`:

- F 的类 C(从成员函数指针推导)= 目标成员类型;
- codegen 生成:目标成员 = `当前执行成员.GetHost()->FindMember(C 类名)`(宿主成员表,§4.1),
  再 `(Target->*F)(args...)`——args 只含方法参数,不传对象;
- 非成员上下文(服务类/自由函数 await 成员):无"当前成员"宿主 → 显式传对象
  `TAwaitable<T::Method>(Obj, args...)`(fallback);自由函数 await 不变。
- 因此 `GetActorMember<T>()` 在成员间 await 场景可省(TAwaitable 的类参数即目标声明)。
- **非成员上下文(服务类/自由函数)获取 target(显式路径)**——走现成寻址链:
  - 有 PlayerId:`FindActorMember<T>(PlayerId)`(框架 helper = `MActorSystem::Find(PlayerId)` +
    `Actor->FindMember<T>()`);或手动两步;
  - 有 actor 指针:直接 `Actor->FindMember<T>()`;
  - 拿到成员对象后显式传:`TAwaitable<T::Method>(Member, args...)`。
  - 隐式解析仅是成员上下文的语法糖,两条路径底层是同一张宿主成员表(§4.2)。
| 持久化 | §3.4 既有(Schema ↔ JSON / 二进制,成员 = 持久化单元) |

**继承序列化顺序(先父后子,对称读回)**——配合 §5 的 `FPlayerRequestBase` 继承:

- `WriteSnapshot`/`ReadSnapshot` **先递归父类,再本类字段(声明序)**——与 C++ 单继承
  对象布局一致(基类子对象在前):
  ```cpp
  if (ParentClass) ParentClass->WriteSnapshot(Object, Ar);   // ① 基类字段(递归)
  for (MProperty* Prop : Properties) Prop->WriteValue(Object, Ar);  // ② 本类声明序
  ```
- 例:`FPlayerUseItemRequest : FPlayerRequestBase{ PlayerId }` + `{ ItemId; Count; }`
  → 线上顺序 `PlayerId | ItemId | Count`;读取按同一顺序对称回读,round-trip 一致。
- 理由:① C++ 布局对齐 ② 版本兼容(§3.5:基类字段恒在前,新字段追加尾部,老数据读位置稳定)
  ③ 实现零特殊(父类字段 offset 相对对象起始,直接复用父类属性表)。
- 多继承(`MNetServerBase, MObject` 形态):按 `AllParentClasses` 顺序递归;业务请求为
  单继承,多继承父类无 `MPROPERTY`,无实际影响。

## 7. 生成产物清单(增量)

| 文件 | 内容 |
|---|---|
| `MPlayerActor.mgenerated.{h,cpp}` | actor 注册表条目(成员关联) |
| `MPlayerItemContainer.mgenerated.{h,cpp}` | member 注册宏 + `GMemberRpcEntries` 条目 |
| manifest 生成 | member ServerCall 条目(OwnerType=member, Target=服务) |

## 8. 验证策略(F1)

1. MHeaderTool 单测:Type=ActorMember 解析 + 生成产物结构。
2. 运行时单测:
   - 成员实例化/挂载/`GetActorMember<T>()`;
   - FunctionId → (member 类, 方法) 查表;
   - 请求带 PlayerId → actor 定位 → member 反射调用(round-trip)。
3. F1 业务验证:登录(member `MPlayerLogin`)+ 背包(member `MPlayerItemContainer`)全链路。

## 9. 风险与决策记录

| 决策/风险 | 结论 |
|---|---|
| IActorMember 是否含 MObject | **含**(复用反射调用链);用户代码写法不变 |
| actor↔成员关联 | **反射扫描 + Meta 绑定**(§3.1):member 声明 `Meta=(Actor=...)`,按目标 actor 过滤挂载 |
| 成员创建方式 | **Component 模型**:`CreateDefaultSubObject<T>(this)`(Object.h 已有,UE 风格);actor 需 MObject 身份(IActor+MObject,仿 MRankListActor) |
| 继承序列化 | **必须支持**(§5 前置):生成器 SetParent 接线 + WriteSnapshot/读取端先父后子递归;否则基类字段丢失、往返不一致 |
| 序列化顺序 | **先父后子,对称读回**(§6):与 C++ 布局一致,版本兼容 |
| FunctionId 域 | (member 类名, 方法名)(§3.2 D1-a),与 ServerCall 一致 |
| 所属服务 | **从 Actor 推导**(§3.3):member 只写 `Meta=(Actor=...)`;actor 类写一次 `Meta=(Service=...)`;生成器推导 manifest TargetName,不逐 member 手写 |
| 成员间 await | **TAwaitable 成员函数指针特化**(§6 E):`TAwaitable<Member::Method>(args...)`(可省 &);成员上下文隐式 target(GetHost→成员表),非成员显式(FindActorMember/FindMember);重载方法需 &+static_cast |
| 进程内部入口 | **MNetServerBase 内置**(§4.3 F1-a+F2-b):单分发入口 `FrameworkMemberDispatch(FunctionId, Payload)`,业务类零改动;GMemberRpcEntries 由 MHeaderTool 生成,零手动声明 |
| 请求寻址 | 请求继承 `FPlayerRequestBase{PlayerId}`(§5);分发器反序列化后 FindProperty 路由 |
| EServerType::Player | 新增 =8;LoginService 复用 Login=2 枚举值 |
| Gateway 改动 | manifest 只路由到进程级(SEntry.TargetName),进程内按 FunctionId 分发,Gateway 保持薄 |
