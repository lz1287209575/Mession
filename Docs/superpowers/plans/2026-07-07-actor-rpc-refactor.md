# 同质多进程 + Actor-based RPC 重构 — 实施计划（带代码版）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** 把当前 6 服异质架构重构为同质多进程 + Actor-based RPC；PoC 跑通 UE→Gateway→ServiceA→ServiceB 跨服调用。

**Architecture (v2, 2026-07-07 重设计)**: 见 `/root/Mession/Docs/superpowers/specs/2026-07-07-actor-rpc-refactor.md`（摘要） + `/root/Mession/Docs/RefactorArchitectureAndRpc.md`（权威）
- **第 1 步 PoC 拓扑（2 进程）**：Gateway(8001) + Echo(7001)。**先保证有一个 Service 跑通链 1**——UE→Gateway→Echo→回包。
- **第 2 步扩展（3 进程）**：再启动 SampleB(7002)，跑链 2（ServiceA→ServiceB 直连）。**第 2 步见文末 "扩展步骤"**。
- **每个进程单一 `ServiceMain.cpp`**：进程入口 + 依赖装配 + 启动逻辑 平铺在一个文件里
- **`ServiceContainer` 轻量 Registry**：进程内全局单例，持有到其他进程的 `MServerConnection` transport map；不参与 MObject 反射树
- **客户端上行**：UE → Gateway → ClientFunctionRoute 查表 → 直接转发到对应 Service
- **跨 Service 转发**：ServiceA → ServiceB 直连（不走 Gateway），通过 ServiceContainer.Resolve(ServiceType) 拿 transport
- **客户端下行**：Service → Gateway (PushClientDownlink) → UE

**Tech Stack:** C++20, MHeaderTool, MReflect, MFUTURE, MActorRouter, MRpcChannel, ServiceContainer

**约定：**
- 所有代码块都是可直接粘贴的最终代码
- 路径以 `/root/Mession/` 为根
- diff 格式用 `- 旧 / + 新`
- **架构 v1 (MWorldServer) 内容已废弃**——所有 `MBackendServiceEndpoint` / `MBackendServiceEndpointFactory` / `MWorldServer::DispatchClientCall` / World 进程相关 task 全部不执行

---

## 编码约定（本计划所有 Phase 适用）

> 这些约定不靠 MHeaderTool 或 clang-tidy 强制，靠代码评审时遵守。

### 禁用 lambda 嵌套（避免"lambda 嵌 lambda"陷阱）

**规则**：
1. **回调体 > 3 行** ⇒ 抽出为命名函数（成员函数 / 命名空间函数），不在调用点写大 lambda。
2. **回调体里有 try/catch / 嵌套 if** ⇒ 同上。
3. **捕获列表里超过 1 个变量** ⇒ 同上；改用 `TWeakPtr<T>::Pin()` 模式把 `this` 转为 weak 引用，避免生命周期耦合。
4. **转发 lambda（仅一行 `Self->Method(...)`）** ⇒ 允许，但回调体必须只有一行 + 不再有嵌套。

**反例**（计划中所有"内嵌大 lambda"的写法都已改）：

```cpp
// BAD：30 行的 lambda 嵌套在 .Then(...) 里
MRpcChannel::Get().CallToActor<...>(...)
    .Then([](MFuture<...> Inner) -> MFuture<...> {
        TResult<...> Result;
        try { ... } catch (...) { ... }
        // ...
        return Promise.GetFuture();
    });

// GOOD：抽出为命名函数
MRpcChannel::Get().CallToActor<...>(...)
    .Then(&AdaptEchoResponseToForwarded);
```

**反例 2**（OnMessage 回调含捕获）：

```cpp
// BAD：在 Initialize() 里塞 lambda，捕获 [this, ServerName]
Connection->SetOnMessage([this, ServerName](auto, uint8, auto) {
    if (PacketType == ...) { return; }
    LOG_WARN("...%s...", ServerName.c_str(), ...);
});

// GOOD：转发到成员函数，回调体只有一行
TWeakPtr<T> WeakSelf = AsShared();
Connection->SetOnMessage([WeakSelf](auto Conn, uint8 P, auto D) {
    if (auto Self = WeakSelf.Pin()) { Self->OnBackendPacket(Conn, P, D); }
});
### 命名函数优先

- 异步适配器：`Handle*Result` 前缀（如 `HandleEchoResult`）
- 注册器：`Register*` 后缀（如 `MEchoService::RegisterLocalActors`）
- 容器：`M*Container::Resolve/Register` 静态方法（见 Task 2.2 `MServiceContainer`）
- 解析器：`Parse*` 前缀的 static 函数（见 Task 2.5 `ParsePort/ParseService/ParsePeers/ParseActorIds`）

### MObject 构造入口 + IDisposable 释放

> **架构 v2 简化**：v2 完全不用 `MBackendServiceEndpoint`——原"WorldServer 平铺 backend 字段"的问题被"ServiceMain 单文件 + ServiceContainer 平铺"取代。Task 6 的 MObject 改造仍按 v1 计划执行（独立排期），但 PoC 阶段不再涉及。

- **构造**：MObject 派生类**唯一**入口 `NewMObject<T>(Outer, Name, ...)` —— 返回 `T*`，内部 `MakeShared` 持有 + `SetOuter` 挂到 Outer.Children。
  - 禁止 `new T(...)` / `TSharedPtr<T>(new T(...))` / `MakeShared<T>` 绕过 NewMObject。
  - MObject 派生类**不走 TSharedPtr 体系**——持引用用 raw pointer `T*`，生命周期归 Outer 管。
- **持有非托管资源的 MObject 派生类必须实现 `IDisposable::Dispose()`**，MObject 析构时兜底调用一次（见 Task 6.1 `~MObject()` 改造 + Task 6.2 `IDisposable` 定义）。
  - `Dispose()` 必须**幂等**（`bDisposed` 标记）。
- **MNetServerBase 派生类（Server / Service）保持栈对象**：`MNetServerBase` 不继承 `TSharedFromThis`，生命周期由 main() 栈帧管，不要把它们 `NewMObject` 进 MObject 树。
  - 例：`MEchoService Service;` 在 `EchoServiceMain.cpp` 是栈对象；其持有的 `MServerConnection` 通过 `MServiceContainer::Get().Register(PeerConn)` 注入到进程内全局 transport map，不要把它们做成 MObject。

### 进程入口 + 依赖装配风格（v2 新增）

- **每个进程单一 `ServiceMain.cpp`**：进程入口 + 命令行参数解析 + 依赖装配 + 启动逻辑全部平铺在一个文件里。
- **平铺 ≠ 全局变量**：所有依赖（listener、peer connections）通过 `MServiceContainer` 全局单例注册，调用方通过 `Resolve(ServiceType)` 查。
- **不要**在 ServiceMain.cpp 之外构造对象（除 EchoService.h 中由 Listener 隐式构造的）。
- **入口函数清单**：`ParsePort/ParseService/ParseLocalType/ParseActorIds/ParsePeers` —— 全部在 unnamed namespace 里定义。

---

## 第零阶段：基建收口（已完成）

### Task 0.1: 删除过时文档

- 状态：完成（55 个文件已标记 `D`）

### Task 0.2: 修复文档重复章节

- 状态：完成

---

## 第一阶段：v2 基础收口（M2）

### Task 1.0: 引入 ServiceId/InstId 位布局工具（v2 重设计核心）

> **动机**：v2 临时名（`Echo`）硬编码到 `EServerType` 全局枚举，加 Service 就要改枚举 + 改代码。改成 `(ServiceId, InstId)` 二元组后，ActorId 自带路由信息，调用方收到响应就能反调。
>
> **位布局**（32+32）：
> ```
> uint64 ActorId
>   └── high 32 bit: ServiceId  (uint32)  —— EServerType 数值别名（7=Echo（PoC）/ 后续生产可改 CombatService=7, 1=Gateway ...）
>   └── low  32 bit: InstId     (uint32)  —— 实例号（同一 Service 类型下的第几个实例）
> ```
>
> **为什么 32+32 而不是 16+48**：
> - 16bit ServiceId 只够 65536 种业务——但生产环境会有几万种业务（玩家账号、地图分区、怪物模板 ID 都要编码）
> - 32bit ServiceId 4G 个空间——接 Lua/资源 ID/账号 ID 都安全
> - 32bit InstId 同理，单进程最多百万级实例足够
>
> **EServerType 别名映射**：
> - EServerType 是 uint8 存但语义值 ≤ 255；ServiceId = static_cast<uint32>(EServerType)
> - ServiceId 通过 EServerType 间接表达——保留 EServerType 枚举做"业务类型集合"约束
> - Echo=7 是 PoC 阶段新增；第 2 步扩展可加 SampleB=8（命名待定）；后续生产可加 CombatService=9, InventoryService=10
> - 临时名不污染业务：枚举值可以叫 Echo（PoC 临时名） 但 `GetServerTypeDisplayName` 返回 "Echo"，生产前再改名

**新文件** `/root/Mession/Source/Servers/App/ServiceId.h`：

```cpp
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Net/ServerConnection.h"  // EServerType

// ActorId 布局：[ServiceId: high 32][InstId: low 32]
//
// ActorId 是 64 位无符号整数；高 32 位是 ServiceId（= EServerType 数值），低 32 位是 InstId。
// 这意味着 ServiceId 实际范围 0 ~ 2^32-1；EServerType 是 uint8 存但语义值 ≤ 255。
// 把 ServiceId 用 uint32 而非 uint8 是为了：未来如果要把 ServiceId 从 EServerType 解耦
// （例如用业务类型字符串哈希作 ServiceId），位布局不用改。

namespace MServiceId
{
    constexpr uint64 ServiceIdShift = 32;
    constexpr uint64 InstIdMask     = 0x00000000FFFFFFFFull;
    constexpr uint64 ServiceIdMask  = 0xFFFFFFFF00000000ull;

    // 构造 ActorId
    inline uint64 Make(EServerType ServiceType, uint32 InstId)
    {
        return (static_cast<uint64>(static_cast<uint32>(ServiceType)) << ServiceIdShift)
             | static_cast<uint64>(InstId);
    }

    inline uint64 Make(uint32 ServiceId, uint32 InstId)
    {
        return (static_cast<uint64>(ServiceId) << ServiceIdShift)
             | static_cast<uint64>(InstId);
    }

    // 提取字段
    inline EServerType GetServiceType(uint64 ActorId)
    {
        return static_cast<EServerType>((ActorId & ServiceIdMask) >> ServiceIdShift);
    }

    inline uint32 GetServiceId(uint64 ActorId)
    {
        return static_cast<uint32>((ActorId & ServiceIdMask) >> ServiceIdShift);
    }

    inline uint32 GetInstId(uint64 ActorId)
    {
        return static_cast<uint32>(ActorId & InstIdMask);
    }

    // EServerType -> ServiceId 字符串（用于日志）。不改 EServerType 枚举本身。
    inline const char* GetServiceTypeName(EServerType Type)
    {
        return GetServerTypeDisplayName(Type);  // 复用现有 helper
    }
}
```

**改动 1（同步）**：`/root/Mession/Source/Common/Net/ServerConnection.h` 在 `EServerType` 枚举里加 Echo（第 1 步）：

```cpp
enum class EServerType : uint32
{
    Unknown = 0,
    Gateway = 1,
    Login = 2,
    World = 3,
    Scene = 4,
    Router = 5,
    Mgo = 6,
    // ↓ v2 PoC 第 1 步：1 个 Service 类型（Echo）。生产部署前改为业务名（如 CombatService=7）
    Echo = 7,  // PoC 第 1 步
    // ↓ v2 PoC 第 2 步扩展：再加 1 个 Service 类型
    // SampleB = 8,
};
```

> **关键点**：
> 1. **ActorId 不是 raw uint64**——业务层看到 ActorId 应理解为 (ServiceId, InstId) 二元组。
> 2. **MServerConnection::SetLocalInfo** 当前用 `uint32 Id, EServerType Type, const char* Name`——ServiceId 直接从 `Id` 字段拿。
> 3. **InstId 0 是合法值**——表示"未指定实例"；调用方需自行判断是否合法（`MActorRouter::FindActor(0)` 返回空）。
> 4. **EServerType 数值 = ServiceId**——别名映射，1=Gateway/2=Login/.../7=Echo 不变（第 1 步）；第 2 步扩展可加 SampleB=8（命名待定）。

**验证**：

```bash
grep -n "Echo\\s*=" Source/Common/Net/ServerConnection.h
# 期望: 找到 1 行

grep -n "MServiceId::Make\|namespace MServiceId" Source/Servers/App/ServiceId.h
# 期望: 找到 2 处
```

### Task 1.1: 删除整个 WorldServer 目录（架构 v2 不再有 World 进程）


**架构 v2 决策**：原本 v1 把 World 作为"中央 dispatcher + Actor 根"进程；v2 直接删除 World 进程，**Gateway 直接转发 ClientCall 到对应 Service**（Service 自己内查 `ClientFunctionRoute` 表？否——见 Phase 3 v2 重构）。

**改动**：

```bash
cd /root/Mession

# 1. 删除整目录
rm -rf Source/Servers/World/

# 2. CMakeLists.txt 删除 WorldServer target（CMakeLists.txt:429-468）
# 用注释替代（参考下方 diff）

# 3. 验证
ls Source/Servers/
# 期望: App Gateway EchoService
grep -n "add_executable(WorldServer\|Source/Servers/World/" CMakeLists.txt
# 期望: 无输出
```

**CMakeLists.txt diff**：

```diff
- # 3. World Server - 世界服务器
- add_executable(WorldServer
-     Source/Servers/World/WorldServer.h
-     Source/Servers/World/WorldServer.cpp
-     Source/Servers/World/WorldServerMain.cpp
- )
- target_link_libraries(WorldServer
-     PRIVATE
-         mession_netdriver
- )
- configure_mession_target(WorldServer)
- mession_attach_generated_groups(WorldServer shared app world)
```

---

### Task 1.2: 删除 ObjectCallRouter / ObjectCallRegistry / ObjectCall .h

> **为什么独立**：Task 1.1 删 WorldServer 时自动带走这些代码（WorldServer 是唯一用户），但 ObjectCall*.h 在 `Source/Servers/App/` 下还有残留——单独清。

**改动**：

```bash
cd /root/Mession

rm -f Source/Servers/App/ObjectCallRouter.h
rm -f Source/Servers/App/ObjectCallRouter.cpp
rm -f Source/Servers/App/ObjectCallRegistry.h
rm -f Source/Servers/App/ObjectCallRegistry.cpp
rm -f Source/Servers/App/ObjectCall.h
```

**CMakeLists.txt diff**（删除 `MESSION_COMMON_SOURCES` 中 ObjectCall 源）：

```diff
   Source/Common/Net/Rpc/RpcDispatch.cpp
   Source/Common/Net/Rpc/RpcManifest.cpp
   Source/Common/Net/Rpc/RpcServerCall.cpp
   Source/Common/Net/Rpc/RpcTransport.cpp
-  Source/Servers/App/ObjectCallRegistry.cpp
-  Source/Servers/App/ObjectCallRouter.cpp
 )
```

**验证**：

```bash
grep -n "ObjectCallRouter\|ObjectCallRegistry\|ObjectCall\b" CMakeLists.txt Source/
# 期望: 无输出

ls Source/Servers/App/
# 期望: 不含 ObjectCall*
```

---

### Task 1.3: 编译验证

```bash
cd /root/Mession
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
cmake --build Build -j4
```

**期望**：编译通过。WorldServer target 已删，Gateway/EchoService 仍能构建。

---

### Task 1.4: 删除 4 个旧 Server target（Login/Scene/Router/Mgo）+ 旧 Server 源码

> **逻辑同 v1 Plan Task 5.1，但前置进 Phase 1**：因为 Gateway 也依赖 Login 旧代码的 include（潜在），提前清。

**改动 1**：`/root/Mession/CMakeLists.txt` 整段删除 4 个 `add_executable`：

```cmake
# 2. Login Server - 登录服务器（删除）
# 4. Scene Server - 场景服务器（删除）
# 5. Router Server - 控制面路由服务器（删除）
# 6. Mgo Server - 持久化接入层服务器（删除）
```

替换为：

```cmake
# Login/Scene/Router/Mgo 已被 EchoService（同质多进程）替代
# 详见 Docs/RefactorArchitectureAndRpc.md §5
```

**改动 2**：删除 4 个旧 Server 源目录：

```bash
cd /root/Mession
rm -rf Source/Servers/Login
rm -rf Source/Servers/Scene
rm -rf Source/Servers/Router
rm -rf Source/Servers/Mgo

ls Source/Servers/
# 期望: App Gateway EchoService
```

**验证**：

```bash
grep -rn "ObjectCallRouter\|ObjectCallRegistry\|MObjectCall" CMakeLists.txt Source/
# 期望: 无输出

grep -rn "add_executable(LoginServer\|add_executable(SceneServer\|add_executable(RouterServer\|add_executable(MgoServer" CMakeLists.txt
# 期望: 无输出
```

## 第二阶段：新增 EchoService + ServiceContainer（M3）

### Task 2.1: 定义 FSampleEchoRequest/Response 协议消息

**新文件** `/root/Mession/Source/Protocol/Messages/EchoService/FSampleEchoMessages.h`：

```cpp
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FSampleEchoRequest
{
    MPROPERTY()
    MString Message;

    // 目标 ActorId（64-bit 位布局：[ServiceId: high 32][InstId: low 32]）。
    // 客户端传过来时是 raw uint64；本进程用 MServiceId::Make/Get* 拆解。
    MPROPERTY(Meta=(NonZero, ErrorCode="actor_id_required", ErrorContext="SampleEcho"))
    uint64 TargetActorId = 0;
};

MSTRUCT()
struct FSampleEchoResponse
{
    MPROPERTY()
    MString Echo;

    // 主叫 ActorId（同样 64-bit 位布局）。远端收到后能用 MServiceId::GetServiceType()
    // 知道主叫来自哪种 Service，再 MServiceContainer::Resolve 拿到 transport 反调。
    MPROPERTY()
    uint64 SourceActorId = 0;

    MPROPERTY()
    MString SourceServerName;
};
```

```bash
mkdir -p /root/Mession/Source/Protocol/Messages/EchoService
```

---

### Task 2.2: 定义 ServiceContainer（轻量 Registry）

> **核心类**：进程内全局单例，持有到其它 Service 进程的 `MServerConnection` transport map。
> **不走 MObject 体系**：纯静态单例（生命周期 = 进程生命周期）；不需要反射、不需要 IDisposable、不需要 Outer 链。
> **依赖注入**：进程启动时（ServiceMain）一次性 `RegisterAll()` 写入所有要连的 peer；运行时通过 `Resolve(ServiceType)` 查 transport。

**新文件** `/root/Mession/Source/Servers/App/ServiceContainer.h`：

```cpp
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Net/ServerConnection.h"

// 进程内全局单例：持有本进程到其它 Service 进程的 MServerConnection transport map。
//
// 用途：任何跨进程 ServerCall 调用方先 Resolve(ServiceType) 拿到 transport，再 CallServerFunction。
//
// 不参与 MObject 反射树；不走 TSharedPtr 体系；进程启动时 RegisterAll() 一次性注入，
// 进程退出时由静态析构自动释放。
class MServiceContainer
{
public:
    static MServiceContainer& Get();

    // 注册一个 transport：建立连接 + 放到 map + 注册到 MServerRuntimeContext。
    // 通常在 ServiceMain::main() 启动阶段批量调用。
    void Register(const TSharedPtr<MServerConnection>& Conn);

    // 通过 EServerType 查 transport；返回 nullptr 表示未注册或连接未建立。
    TSharedPtr<MServerConnection> Resolve(EServerType ServerType) const;

    // 关闭所有 connection + 清空 map。在 ServiceMain 退出前调用。
    void ShutdownAll();

    // Tick 所有 connection。
    void TickAll(float DeltaTime);

private:
    MServiceContainer() = default;

    TMap<EServerType, TSharedPtr<MServerConnection>> Connections;
};
```

**新文件** `/root/Mession/Source/Servers/App/ServiceContainer.cpp`：

```cpp
#include "Servers/App/ServiceContainer.h"
#include "Common/Runtime/Log/Logger.h"

MServiceContainer& MServiceContainer::Get()
{
    static MServiceContainer Instance;
    return Instance;
}

void MServiceContainer::Register(const TSharedPtr<MServerConnection>& Conn)
{
    if (!Conn)
    {
        return;
    }
    const EServerType ServerType = Conn->GetConfig().ServerType;
    Connections[ServerType] = Conn;
    LOG_INFO("ServiceContainer: registered peer %s (%s:%u)",
             GetServerTypeDisplayName(ServerType),
             Conn->GetConfig().Address.c_str(),
             static_cast<unsigned>(Conn->GetConfig().Port));
}

TSharedPtr<MServerConnection> MServiceContainer::Resolve(EServerType ServerType) const
{
    auto It = Connections.find(ServerType);
    return (It != Connections.end()) ? It->second : nullptr;
}

void MServiceContainer::ShutdownAll()
{
    for (auto& [Type, Conn] : Connections)
    {
        (void)Type;
        if (Conn)
        {
            Conn->Disconnect();
        }
    }
    Connections.clear();
}

void MServiceContainer::TickAll(float DeltaTime)
{
    for (auto& [Type, Conn] : Connections)
    {
        (void)Type;
        if (Conn)
        {
            Conn->Tick(DeltaTime);
        }
    }
}
```

---

### Task 2.3: 定义 MEchoService 类（头文件）

**新文件** `/root/Mession/Source/Servers/EchoService/EchoService.h`：

```cpp
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/Rpc/RpcRuntimeContext.h"
#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Log/Logger.h"
#include "Protocol/Messages/EchoService/FSampleEchoMessages.h"

// 进程间连接 peer 配置。PoC 阶段由 --peers 命令行参数解析；每个 peer 一项。
struct SServicePeerConfig
{
    EServerType ServerType = EServerType::Unknown;
    MString Address = "127.0.0.1";
    uint16 Port = 0;
};

struct SEchoServiceConfig
{
    uint16 ListenPort = 0;
    MString ServiceName = "MEchoService";
    EServerType LocalServerType = EServerType::Unknown;
    uint32 LocalServerId = 0;
    uint32 LocalInstId = 0;  // 本进程在 LocalServerType 类型下的实例号
    TVector<uint32> LocalActorIds;  // 本进程持有的其他 Actor 的 InstId 列表；最终 ActorId = MServiceId::Make(LocalServerType, InstId)
    TVector<SServicePeerConfig> Peers;  // 启动时连接的 peer 列表（Gateway + 其它 EchoService）
};

MCLASS(Type=Service)
class MEchoService : public MNetServerBase, public MObject, public MServerRuntimeContext
{
public:
    MGENERATED_BODY(MEchoService, MObject, 0)
public:
    using MObject::Tick;

    bool LoadConfig(const MString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void ShutdownConnections() override;
    void OnRunStarted() override;

    void ApplyConfig(const SEchoServiceConfig& InConfig) { Config = InConfig; }

    // EchoService 暴露的 ServerCall——Gateway 通过 ClientFunctionRoute 路由表查到本类后调用。
    MFUNCTION(ServerCall)
    MFuture<TResult<FSampleEchoResponse, FAppError>> Echo(const FSampleEchoRequest& Request);

private:
    // 建立到所有 peer 的连接 + 注册到 MServiceContainer + 注册到本进程 MServerRuntimeContext。
    void ConnectAllPeers();

    // 本机 Actor 注册到 MActorRouter（ServerType=Unknown 表示本机）。
    void RegisterLocalActors();

    SEchoServiceConfig Config;
    TMap<uint64, MString> ActorMessages;
};
```

```bash
mkdir -p /root/Mession/Source/Servers/EchoService
```

---

### Task 2.4: 实现 MEchoService（cpp）

**新文件** `/root/Mession/Source/Servers/EchoService/EchoService.cpp`：

```cpp
#include "Servers/EchoService/EchoService.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Servers/App/ServiceContainer.h"
#include "Common/Runtime/Log/Logger.h"

bool MEchoService::LoadConfig(const MString& /*ConfigPath*/)
{
    return true;
}

bool MEchoService::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    if (Config.ListenPort == 0)
    {
        LOG_ERROR("EchoService: ListenPort is 0");
        return false;
    }

    bRunning = true;
    MLogger::LogStartupBanner(Config.ServiceName.c_str(), Config.ListenPort, 0);

    // 标记本进程本地 Server 信息——MServerConnection 响应包分发时依赖 LocalInfo。
    MServerConnection::SetLocalInfo(Config.LocalServerId, Config.LocalServerType, Config.ServiceName.c_str());

    RegisterLocalActors();
    ConnectAllPeers();

    return true;
}

void MEchoService::Tick()
{
    MServiceContainer::Get().TickAll(0.0f);
}

uint16 MEchoService::GetListenPort() const
{
    return Config.ListenPort;
}

void MEchoService::OnAccept(uint64 /*ConnId*/, TSharedPtr<INetConnection> /*Conn*/)
{
    // PoC 阶段不接受 peer 连接（EchoService 不监听 client TCP——只有 Gateway 监听）
}

void MEchoService::ShutdownConnections()
{
    MServiceContainer::Get().ShutdownAll();

    for (uint32 InstId : Config.LocalActorIds)
    {
        const uint64 ActorId = MServiceId::Make(Config.LocalServerType, InstId);
        MActorRouter::Get().UnregisterActor(ActorId);
    }
    ActorMessages.clear();

    ClearRpcTransports();
}

void MEchoService::OnRunStarted()
{
    LOG_INFO("%s running on port %u",
             Config.ServiceName.c_str(),
             static_cast<unsigned>(Config.ListenPort));
}

void MEchoService::RegisterLocalActors()
{
    for (uint32 InstId : Config.LocalActorIds)
    {
        // 本机 Actor 标记 EServerType::Unknown——MActorRouter::SendToActor 在
        // ServerType == Unknown 时走 IsActorLocal 分支（见 ActorRouter.cpp:42-46）。
        const uint64 ActorId = MServiceId::Make(Config.LocalServerType, InstId);
        MActorRouter::Get().RegisterActor(ActorId, EServerType::Unknown);
        ActorMessages[ActorId] = MString();
        LOG_INFO("%s: registered local actor ServiceId=%u InstId=%u (ActorId=%llu)",
                 Config.ServiceName.c_str(),
                 static_cast<unsigned>(MServiceId::GetServiceId(ActorId)),
                 static_cast<unsigned>(MServiceId::GetInstId(ActorId)),
                 static_cast<unsigned long long>(ActorId));
    }
}

void MEchoService::ConnectAllPeers()
{
    for (const SServicePeerConfig& Peer : Config.Peers)
    {
        const uint32 PeerServerId = MUniqueIdGenerator::Generate();
        const SServerConnectionConfig PeerConfig(
            PeerServerId,
            Peer.ServerType,
            GetServerTypeDisplayName(Peer.ServerType),
            Peer.Address,
            Peer.Port);

        TSharedPtr<MServerConnection> PeerConn = MakeShared<MServerConnection>(PeerConfig);
        PeerConn->Connect();

        // 1. 注册到 MServiceContainer（全局 transport map）
        MServiceContainer::Get().Register(PeerConn);

        // 2. 注册到本进程 MServerRuntimeContext（用于 RpcRuntimeContext::ResolveServerTransport）
        RegisterRpcTransport(Peer.ServerType, PeerConn);

        LOG_INFO("%s: connected to peer %s at %s:%u",
                 Config.ServiceName.c_str(),
                 GetServerTypeDisplayName(Peer.ServerType),
                 Peer.Address.c_str(),
                 static_cast<unsigned>(Peer.Port));
    }
}

MFuture<TResult<FSampleEchoResponse, FAppError>> MEchoService::Echo(const FSampleEchoRequest& Request)
{
    if (Request.TargetActorId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSampleEchoResponse>(
            "actor_id_required", "Echo");
    }

    // 从 TargetActorId 拆解目标 Service 类型
    const EServerType TargetServiceType = MServiceId::GetServiceType(Request.TargetActorId);
    const uint32 TargetInstId = MServiceId::GetInstId(Request.TargetActorId);

    LOG_INFO("%s: Echo received TargetServiceType=%s TargetInstId=%u",
             Config.ServiceName.c_str(),
             GetServerTypeDisplayName(TargetServiceType),
             static_cast<unsigned>(TargetInstId));

    const SActorRoute Route = MActorRouter::Get().FindActor(Request.TargetActorId);

    // 响应中带回本进程的 (ServiceId, InstId) 让远端能反调
    const uint64 SelfActorId = MServiceId::Make(Config.LocalServerType, Config.LocalInstId);

    FSampleEchoResponse Response;
    Response.Echo = Request.Message + " [echoed]";
    Response.SourceActorId = SelfActorId;        // 远端用此反查
    Response.SourceServerName = Config.ServiceName;

    // 本机 Actor：直接返
    if (Route.ActorId != 0 && Route.ServerType == EServerType::Unknown)
    {
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    // 远程 Actor：跨进程转发（链 2 路径——ServiceA → ServiceB 直连，不走 Gateway）
    if (Route.ActorId != 0)
    {
        // 通过 MServiceContainer 查 peer transport
        TSharedPtr<MServerConnection> TargetConn = MServiceContainer::Get().Resolve(TargetServiceType);
        if (!TargetConn || !TargetConn->IsConnected())
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FSampleEchoResponse>(
                "peer_transport_unavailable", "Echo");
        }

        // 直接转发到 peer——Service B 处理完会回 A，A 再回原始调用方（Gateway）。
        // 这里调用方可能是 Gateway（ClientCall 入口）或另一 Service（链 2 内部转发）。
        return CallServerFunction<FSampleEchoResponse>(
            TargetConn, "MEchoService", "Echo", Request);
    }

    return MServerCallAsyncSupport::MakeErrorFuture<FSampleEchoResponse>(
        "actor_not_found", "Echo");
}
```

---

### Task 2.5: 实现 EchoServiceMain（单一 main 文件 + ServiceInject 平铺）

> **v2 核心改动**：进程入口 + 依赖装配 + 启动逻辑全部平铺在 `EchoServiceMain.cpp` 一个文件里。
> 不用 WorldServer 那种 Actor 树架构，所有依赖（Listener、ServiceContainer、PeerConn）都在 main() 里直接构造 + 装配。

**新文件** `/root/Mession/Source/Servers/EchoService/EchoServiceMain.cpp`：

```cpp
#include "Servers/EchoService/EchoService.h"
#include "Servers/App/ServiceContainer.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Net/Rpc/RpcRuntimeContext.h"

#include <cstdlib>

namespace
{
// 解析 --listen=PORT
uint16 ParsePort(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg.StartsWith("--listen="))
        {
            return static_cast<uint16>(atoi(Arg.SubStr(MString("--listen=").Size()).c_str()));
        }
    }
    return 0;
}

// 解析 --service=MEchoService
MString ParseService(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg.StartsWith("--service="))
        {
            return Arg.SubStr(MString("--service=").Size());
        }
    }
    return MString("MEchoService");
}

// 解析 --local-type=Echo 或 --local-type=SampleB（决定本进程的 LocalServerType）
EServerType ParseLocalType(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg.StartsWith("--local-type="))
        {
            MString Value = Arg.SubStr(MString("--local-type=").Size());
            if (Value == "Echo") return EServerType::Echo;
            // 第 2 步扩展: if (Value == "SampleB") return EServerType::SampleB;
            if (Value == "Gateway") return EServerType::Gateway;
        }
    }
    return EServerType::Unknown;
}

// 解析 --actors=1,2,3 — 每个值是 InstId（uint32），最终 ActorId 拼上 LocalServerType
TVector<uint32> ParseActorIds(int argc, char** argv)
{
    TVector<uint32> Result;
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg.StartsWith("--actors="))
        {
            MString Value = Arg.SubStr(MString("--actors=").Size());
            size_t Pos = 0;
            while (Pos < Value.Size())
            {
                size_t CommaPos = Value.find(',', Pos);
                MString Token = (CommaPos == MString::NPos)
                    ? Value.SubStr(Pos)
                    : Value.SubStr(Pos, CommaPos - Pos);
                uint64 Id = 0;
                if (MStringUtil::TryParseUint64(Token, Id) && Id != 0 && Id <= 0xFFFFFFFFu)
                {
                    Result.push_back(static_cast<uint32>(Id));
                }
                if (CommaPos == MString::NPos) break;
                Pos = CommaPos + 1;
            }
        }
    }
    return Result;
}

// 解析 --peers=Gateway@127.0.0.1:8001,SampleB@127.0.0.1:7002
TVector<SServicePeerConfig> ParsePeers(int argc, char** argv)
{
    TVector<SServicePeerConfig> Result;
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg.StartsWith("--peers="))
        {
            MString Value = Arg.SubStr(MString("--peers=").Size());
            size_t Pos = 0;
            while (Pos < Value.Size())
            {
                size_t CommaPos = Value.find(',', Pos);
                MString Token = (CommaPos == MString::NPos)
                    ? Value.SubStr(Pos)
                    : Value.SubStr(Pos, CommaPos - Pos);

                // 格式：<Type>@<Addr>:<Port>  例如 Gateway@127.0.0.1:8001
                size_t AtPos = Token.find('@');
                size_t ColonPos = Token.rfind(':');
                if (AtPos != MString::NPos && ColonPos != MString::NPos && ColonPos > AtPos)
                {
                    SServicePeerConfig Peer;
                    MString TypeName = Token.SubStr(0, AtPos);
                    MString AddrPort = Token.SubStr(AtPos + 1);

                    if (TypeName == "Gateway") Peer.ServerType = EServerType::Gateway;
                    else if (TypeName == "Echo") Peer.ServerType = EServerType::Echo;
                    // 第 2 步扩展: else if (TypeName == "SampleB") Peer.ServerType = EServerType::SampleB;
                    else continue;

                    size_t InnerColon = AddrPort.find(':');
                    Peer.Address = AddrPort.SubStr(0, InnerColon);
                    Peer.Port = static_cast<uint16>(atoi(AddrPort.SubStr(InnerColon + 1).c_str()));
                    Result.push_back(Peer);
                }

                if (CommaPos == MString::NPos) break;
                Pos = CommaPos + 1;
            }
        }
    }
    return Result;
}

// 从 EServerType 派生默认 LocalServerId（PoC 阶段硬编码）
uint32 DefaultServerId(EServerType Type)
{
    switch (Type)
    {
        case EServerType::Echo: return 7;
        // 第 2 步扩展: case EServerType::SampleB: return 8;
        case EServerType::Gateway: return 1;
        default: return 0;
    }
}
}

// 解析 --inst=N（本进程在 LocalServerType 下的实例号）
uint32 ParseInst(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        MString Arg = argv[i] ? argv[i] : "";
        if (Arg.StartsWith("--inst="))
        {
            return static_cast<uint32>(atoi(Arg.SubStr(MString("--inst=").Size()).c_str()));
        }
    }
    return 0;
}

int main(int argc, char** argv)
{
    // === ServiceInject 平铺：进程入口一次性装配所有依赖 ===

    SEchoServiceConfig Config;
    Config.ListenPort = ParsePort(argc, argv);
    Config.ServiceName = ParseService(argc, argv);
    Config.LocalServerType = ParseLocalType(argc, argv);
    Config.LocalServerId = DefaultServerId(Config.LocalServerType);
    Config.LocalActorIds = ParseActorIds(argc, argv);
    Config.LocalInstId = ParseInst(argc, argv);  // 默认 0；命令行 --inst=N 覆盖
    Config.Peers = ParsePeers(argc, argv);

    MEchoService Service;
    Service.ApplyConfig(Config);

    if (!Service.Init(Config.ListenPort))
    {
        LOG_ERROR("EchoService init failed");
        return 1;
    }

    Service.Run();
    return 0;
}
```

---

### Task 2.6: CMakeLists.txt 添加 EchoService target

```diff
+ # EchoService - 同质多进程 Service（PoC 替代 Login/Scene/Router/Mgo 4 服）
+ add_executable(EchoService
+     Source/Servers/EchoService/EchoService.h
+     Source/Servers/EchoService/EchoService.cpp
+     Source/Servers/EchoService/EchoServiceMain.cpp
+     Source/Servers/App/ServiceContainer.h
+     Source/Servers/App/ServiceContainer.cpp
+ )
+ target_link_libraries(EchoService
+     PRIVATE
+         mession_netdriver
+ )
+ configure_mession_target(EchoService)
+ mession_attach_generated_groups(EchoService shared sample)
```

---

### Task 2.7: 编译 + 启动 3 个进程（dry-run，不验证）

```bash
cd /root/Mession
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
cmake --build Build -j4
```

**期望**：编译通过。Gateway 进程仍能启动；EchoService 进程启动参数解析正确但**不验证跨进程 RPC**——见 Phase 4。
## 第三阶段：Gateway 端 ClientCall 直转（M4）

> **架构 v2 关键变更**：原 v1 把"ClientFunctionRoute 查表 + CallToActor"放在 World 进程做；v2 直接在 **Gateway** 端完成——Gateway 是 ClientCall 的唯一入口，路由表 + transport 都在 Gateway 内。
>
> 链路（v2）：
> 1. UE → Gateway (MT_FunctionCall) → `ClientFunctionRoute` 查 `(ServiceType, MethodName)`
> 2. Gateway 用 `MServiceContainer::Resolve(ServiceType)` 拿对应 EchoService 的 transport
> 3. Gateway `CallServerFunction<...>(TargetConn, ClassName, MethodName, ServiceRequest)` 转发
> 4. EchoService 处理完 → 通过 Gateway ConnId 反向 PushClientDownlink 回 UE

### Task 3.1: 定义 ClientFunctionRoute 表

> 跟 v1 完全一致——路由表与具体进程无关。

**新文件** `/root/Mession/Source/Protocol/Messages/Common/ClientFunctionRoute.h`：

```cpp
#pragma once

#include "Common/Runtime/MLib.h"

struct FClientFunctionRoute
{
    uint16 ClientFunctionId = 0;
    EServerType TargetServiceType = EServerType::Unknown;
    const char* ClassName = nullptr;
    const char* MethodName = nullptr;
    bool bRequiresActorId = false;
};

namespace MClientFunctionRouteTable
{
inline const TVector<FClientFunctionRoute>& GetRoutes()
{
    static const TVector<FClientFunctionRoute> Routes = {
        // PoC 第 1 步：仅 Echo 一条；第 2 步再加 SampleB 路由
        { 8801, EServerType::Echo, "MEchoService", "Echo", /*bRequiresActorId=*/true },
    };
    return Routes;
}

inline const FClientFunctionRoute* FindRoute(uint16 ClientFunctionId)
{
    for (const auto& Route : GetRoutes())
    {
        if (Route.ClientFunctionId == ClientFunctionId)
        {
            return &Route;
        }
    }
    return nullptr;
}
}
```

> **注**：路由条目直接给出 ServiceType 而不是 ServiceName——v2 走 transport map 寻址，不需要 ServiceName 解析步骤。

---

### Task 3.2: Gateway 端 DispatchClientCall 实现

> **完全替代 v1 的 World 端 DispatchClientCall**——直接在 Gateway 进程处理。

`/root/Mession/Source/Servers/Gateway/GatewayServer.cpp::HandleClientPacket` 改造：

```cpp
void MGatewayServer::HandleClientPacket(uint64 ConnectionId, const TByteArray& Data)
{
    uint16 FunctionId = 0;
    uint64 CallId = 0;
    uint32 PayloadSize = 0;
    size_t PayloadOffset = 0;
    if (!ParseClientCallPacket(Data, FunctionId, CallId, PayloadSize, PayloadOffset))
    {
        LOG_WARN("Gateway client packet parse failed: connection=%llu",
                 static_cast<unsigned long long>(ConnectionId));
        return;
    }

    TByteArray Payload;
    if (PayloadSize > 0)
    {
        Payload.insert(
            Payload.end(),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    const FClientFunctionRoute* Route = MClientFunctionRouteTable::FindRoute(FunctionId);
    if (!Route)
    {
        // 现有逻辑：未知 ClientFunctionId 发回错误响应
        // ... 略
        return;
    }

    // 解析 Payload 头 8 字节为 ActorId
    uint64 ActorId = 0;
    if (Route->bRequiresActorId)
    {
        if (Payload.size() < sizeof(uint64))
        {
            // 发回 actor_id_required 错误
            return;
        }
        std::memcpy(&ActorId, Payload.data(), sizeof(uint64));
    }

    // 通过 MServiceContainer 查 peer transport
    TSharedPtr<MServerConnection> TargetConn = MServiceContainer::Get().Resolve(Route->TargetServiceType);
    if (!TargetConn || !TargetConn->IsConnected())
    {
        // 发回 transport_unavailable 错误
        return;
    }

    // 拆包：头部 8 字节 ActorId + 后续 Service 请求
    FSampleEchoRequest ServiceRequest;
    if (Payload.size() >= sizeof(uint64))
    {
        if (!ParsePayload(
                TByteArray(Payload.begin() + sizeof(uint64), Payload.end()),
                ServiceRequest,
                "FSampleEchoRequest"))
        {
            // payload_parse_failed
            return;
        }
    }
    ServiceRequest.TargetActorId = ActorId;

    // 通过 ServerCall 转发给 EchoService
    FClientCallHandle Handle(FunctionId, ConnectionId, CallId);
    CallServerFunction<FClientCallResponse>(TargetConn, Route->ClassName, Route->MethodName, ServiceRequest)
        .Then(&HandleEchoResult);
}
```

**`HandleEchoResult` 适配器**（命名空间函数，非 lambda）：

```cpp
namespace
{
// 类型不对称适配：把 FSampleEchoResponse 包成 FClientCallResponse + 发回 UE。
// 单独抽出避免 .Then(...) 内嵌大块 lambda 形成"lambda 嵌 lambda"的可读性陷阱。
MFuture<TResult<SEmptyServerMessage, FAppError>> HandleEchoResult(
    MFuture<TResult<FSampleEchoResponse, FAppError>> Inner,
    FClientCallHandle Handle)
{
    TResult<SEmptyServerMessage, FAppError> OutResult;
    try
    {
        const TResult<FSampleEchoResponse, FAppError> InnerResult = Inner.Get();
        if (InnerResult.IsErr())
        {
            OutResult = TResult<SEmptyServerMessage, FAppError>::Err(InnerResult.GetError());
        }
        else
        {
            // 把 FSampleEchoResponse 序列化后通过 PushClientDownlink 发回 UE
            const FSampleEchoResponse& Resp = InnerResult.GetValue();
            TByteArray Payload;
            BuildPayload(Resp, Payload);

            MGatewayServer* Gateway = MGatewayServer::GetSingleton();
            if (Gateway)
            {
                Gateway->PushClientDownlink(Handle.ConnectionId, Handle.FunctionId, Payload);
            }

            OutResult = TResult<SEmptyServerMessage, FAppError>::Ok(SEmptyServerMessage{});
        }
    }
    catch (const std::exception& Ex)
    {
        OutResult = TResult<SEmptyServerMessage, FAppError>::Err(
            FAppError::Make("handle_echo_exception", Ex.what()));
    }

    MPromise<TResult<SEmptyServerMessage, FAppError>> Promise;
    Promise.SetValue(std::move(OutResult));
    return Promise.GetFuture();
}
}
```

---

### Task 3.3: Gateway 启动时注册 EchoService peer

Gateway `ServiceContainer` 必须持有到每个 EchoService 的 transport。

`GatewayServiceMain`（或 `GatewayServerMain.cpp`）：

```cpp
int main(int argc, char** argv)
{
    // 解析 --peers=Echo@127.0.0.1:7001
    TVector<SServicePeerConfig> Peers = ParsePeers(argc, argv);

    for (const SServicePeerConfig& Peer : Peers)
    {
        const SServerConnectionConfig PeerConfig(
            MUniqueIdGenerator::Generate(),
            Peer.ServerType,
            GetServerTypeDisplayName(Peer.ServerType),
            Peer.Address,
            Peer.Port);

        TSharedPtr<MServerConnection> PeerConn = MakeShared<MServerConnection>(PeerConfig);
        PeerConn->Connect();
        MServiceContainer::Get().Register(PeerConn);
    }

    MGatewayServer Gateway;
    // ... Init + Run
    return 0;
}
```

---

### Task 3.4: 编译验证

```bash
cd /root/Mession
cmake --build Build -j4
```

---

## 第四阶段：测试脚本（M5）

### Task 4.1: 启动脚本 `Scripts/start_service.py`（参数化）

> **设计**：用一个统一脚本按 `--services` 参数启动一组 service。加新 service 只在 `SERVICES` dict 里加 1 行配置，不需要改 `start/stop/status` 流程。

**新文件** `/root/Mession/Scripts/start_service.py`：

```python
#!/usr/bin/env python3
"""
start_service.py — 同质多进程 Service 的统一启动器

按 --services 参数启动一组 Service 进程（gateway / echo / 后续扩展的任意 service）。
子进程 PID 记录到 ./Logs/<svc>.pid；日志到 ./Logs/<svc>.log。

用法:
  ./Scripts/start_service.py start --services=gateway,echo
  ./Scripts/start_service.py status
  ./Scripts/start_service.py stop
  ./Scripts/start_service.py restart --services=echo

设计:
  - SERVICES dict 集中所有 service 的启动参数 + bin 路径
  - 加新 service 只在 dict 里加 1 项；start() 自动处理 --peers 拼接
  - --peers 字段根据 peers_field 标识动态注入：
      "service_peers" → 连 Gateway + 其它 service_peers
      "gateway_peers"  → 连所有其它 service（仅 Gateway 用）
"""
import argparse
import os
import signal
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).parent.parent
BIN = ROOT / "Bin"
LOG_DIR = ROOT / "Logs"
PID_DIR = LOG_DIR


def parse_listen_port(args):
    """从 args 里提取 --listen=N 的 N"""
    for a in args:
        if a.startswith("--listen="):
            return int(a.split("=", 1)[1])
    return 0


# 各 Service 启动参数集中配置——加新 Service 只改这里
SERVICES = {
    "gateway": {
        "bin": BIN / "GatewayServer",
        "args": [
            "--listen=8001",
            "--local-type=Gateway",
        ],
        "peers_field": "gateway_peers",
        "log_name": "gateway",
    },
    "echo": {
        "bin": BIN / "EchoService",
        "args": [
            "--listen=7001",
            "--service=MEchoService",
            "--local-type=Echo",
            "--inst=1",
            "--actors=1,2",
        ],
        "peers_field": "service_peers",
        "log_name": "echo",
    },
    # 第 2 步扩展示例：再加一个 service
    # "inventory": {
    #     "bin": BIN / "EchoService",  # 同一二进制，靠 --local-type 区分业务
    #     "args": ["--listen=7003", "--local-type=Inventory", "--inst=1"],
    #     "peers_field": "service_peers",
    #     "log_name": "inventory",
    # },
}


def build_service_args(svc_name, all_services):
    """根据选定的 services 集合动态拼接 --peers 参数"""
    spec = SERVICES[svc_name]
    args = list(spec["args"])

    # 收集其它需要作为 peer 的服务
    other_service_peers = [
        (name.upper(), parse_listen_port(SERVICES[name]["args"]))
        for name in all_services
        if name != svc_name
    ]

    if spec.get("peers_field") == "service_peers":
        # 普通 service：连 Gateway + 其它 service
        if other_service_peers:
            peers_str = ",".join(f"{n}@127.0.0.1:{p}" for n, p in other_service_peers)
            args.append(f"--peers={peers_str}")
    elif spec.get("peers_field") == "gateway_peers":
        # Gateway：连所有其它 service
        if other_service_peers:
            peers_str = ",".join(f"{n}@127.0.0.1:{p}" for n, p in other_service_peers)
            args.append(f"--peers={peers_str}")

    return args


procs = {}  # svc_name -> (Popen, log_fh)


def parse_services_arg(arg):
    return [s.strip() for s in arg.split(",") if s.strip()]


def start(services):
    if not services:
        services = list(SERVICES.keys())
        print(f"  (未指定 --services，默认启动全部: {services})")

    unknown = [s for s in services if s not in SERVICES]
    if unknown:
        print(f"  [ERROR] 未知 service: {unknown}；已知: {list(SERVICES.keys())}")
        return False

    LOG_DIR.mkdir(parents=True, exist_ok=True)

    for name in services:
        spec = SERVICES[name]
        if not spec["bin"].exists():
            print(f"  [ERROR] {name}: binary 不存在: {spec['bin']}")
            print(f"          请先编译: cmake --build Build -j4")
            return False

        args = [str(spec["bin"])] + build_service_args(name, services)
        log_file = LOG_DIR / f"{spec['log_name']}.log"
        pid_file = PID_DIR / f"{spec['log_name']}.pid"
        log_fh = open(log_file, "w")
        proc = subprocess.Popen(args, cwd=str(ROOT), stdout=log_fh, stderr=subprocess.STDOUT)
        procs[name] = (proc, log_fh)
        pid_file.write_text(str(proc.pid))
        print(f"  [+] {name} pid={proc.pid} log={log_file}")
        time.sleep(0.5)

    return True


def stop():
    if not procs:
        for name, spec in SERVICES.items():
            pid_file = PID_DIR / f"{spec['log_name']}.pid"
            if pid_file.exists():
                try:
                    pid = int(pid_file.read_text().strip())
                    os.kill(pid, signal.SIGTERM)
                    print(f"  [-] {name} pid={pid} (from pidfile)")
                except (ProcessLookupError, ValueError):
                    pass
                pid_file.unlink(missing_ok=True)
        return

    for name, (proc, log_fh) in procs.items():
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
        log_fh.close()
        (PID_DIR / f"{SERVICES[name]['log_name']}.pid").unlink(missing_ok=True)
        print(f"  [-] {name} pid={proc.pid}")
    procs.clear()


def status():
    if not procs:
        for name, spec in SERVICES.items():
            pid_file = PID_DIR / f"{spec['log_name']}.pid"
            if pid_file.exists():
                pid = pid_file.read_text().strip()
                print(f"  {name}: pid={pid} (pidfile only)")
        return

    for name, (proc, _) in procs.items():
        rc = proc.poll()
        alive = "running" if rc is None else f"exited(rc={rc})"
        print(f"  {name}: {alive} pid={proc.pid}")


def main():
    parser = argparse.ArgumentParser(description="同质多进程 Service 启动器")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_start = sub.add_parser("start", help="启动一组 service")
    p_start.add_argument("--services", type=str,
                         help="逗号分隔的 service 名（不传则启动全部）")

    sub.add_parser("stop", help="停止所有已启动的 service")
    sub.add_parser("status", help="查看运行状态")
    p_restart = sub.add_parser("restart", help="stop + start")
    p_restart.add_argument("--services", type=str)

    args = parser.parse_args()

    if args.cmd == "start":
        services = parse_services_arg(args.services) if args.services else None
        if not start(services or []):
            exit(1)
    elif args.cmd == "restart":
        services = parse_services_arg(args.services) if args.services else None
        stop()
        time.sleep(0.5)
        if not start(services or []):
            exit(1)
    elif args.cmd == "stop":
        stop()
    elif args.cmd == "status":
        status()


if __name__ == "__main__":
    main()
```

```bash
chmod +x /root/Mession/Scripts/start_service.py
```

**关键设计点**：

1. **配置集中**：所有 service 启动参数在 `SERVICES` dict 里——加新 service 只加 1 项
2. **peers 动态拼接**：`build_service_args()` 根据 `--services` 参数自动算 `--peers`，无需人工维护
3. **同二进制多 service**：第 2 步加 `inventory` 时复用 `EchoService` 二进制，靠 `--local-type=Inventory` 区分业务
4. **log_name 解耦**：log 文件名跟 service 名解耦（`log_name` 字段），未来可重命名 service 不影响日志目录命名
5. **pid 文件兜底**：stop() 在 procs 字典为空时也能根据 pid 文件清理（防异常退出残留）

### Task 4.2: 验收

```bash
cd /root/Mession

# 默认启动全部（Gateway + Echo）
python3 Scripts/start_service.py start
sleep 2
python3 Scripts/start_service.py status
# 期望: 2 个进程都 running

# 或指定子集
python3 Scripts/start_service.py start --services=echo
python3 Scripts/start_service.py stop

grep "registered local actor" Logs/echo.log
# 期望: 2 条 actor 注册日志，含 "ServiceId=7 InstId=1" 和 "ServiceId=7 InstId=2"（Echo=7）

grep "ServiceContainer: registered peer" Logs/*.log
# 期望: 每个进程都注册了它要连的 peers

python3 Scripts/start_service.py stop
```

**链 1 端到端验证**（PoC 第 1 步手工跑）：

```bash
# 链 1: UE 模拟 → Gateway → Echo.Echo → UE
# 1. 计算 ActorId = MServiceId::Make(Echo, Inst=1) = (7 << 32) | 1 = 0x0000000700000001
# 2. 用 telnet 或 nc 连 127.0.0.1:8001，发 MT_FunctionCall(13) + ClientFunctionId=8801
#    + Payload 头部 8 字节 ActorId=0x0000000700000001（小端）+ "hello"
# 3. 期望收到响应：SourceActorId 高 32 bit=7（Echo），SourceServerName="MEchoService"，Echo="hello [echoed]"
#
# 详细包格式见 Docs/RefactorArchitectureAndRpc.md §4.2
```

**AC-1 ~ AC-4 验收**（PoC 第 1 步通过门槛）：
- [ ] AC-1: Gateway 启动监听 8001，5s 内接受 TCP
- [ ] AC-2: Echo 启动监听 7001，5s 内接受 TCP
- [ ] AC-3: Gateway 启动时连接 Echo（--peers=Echo@...），双向 Connected
- [ ] AC-4: 链 1 跳通：ClientCall(8801) → Echo.Echo(Inst=1) → UE 收到 "hello [echoed]" + SourceActorId 含 (ServiceId=7, Inst=1)

**AC-5 ~ AC-8 暂不实现**（等第 2 步加 SampleB 再说）。


---

## 第五阶段：清理（M6）

### Task 5.0: 代码 cleanup

> **彻底清理架构 v1 残留**：WorldServer 目录已删（Task 1.1）、4 服 target 已删（Task 1.4）、ObjectCall* 已删（Task 1.2）。

PoC 跑通后，**执行前**完整跑一遍下面 6 项 grep + 编辑，把 Phase 1~4 留下的死引用全部清掉：

**1. 死 include 清理**：

```bash
grep -rln "Protocol/Messages/Common/ObjectCallMessages.h" Source/
# 期望: 无输出（Task 1.1 + 1.2 已删）

grep -rln "ObjectCallRouter.h\|ObjectCallRegistry.h\|ObjectCall.h\|MBackendServiceEndpoint" Source/
# 期望: 无输出
```

**2. 死类型引用清理**：

```bash
grep -rn "FObjectCallRequest\|FObjectCallResponse\|IObjectCallRegistryProvider\|MObjectCallRouter\|MBackendServiceEndpoint\|MWorldServer" Source/
# 期望: 无输出
```

**3. Dead macro 清理**：

```bash
grep -rn "PersistenceSubsystem\|DestroyMObject" Source/
# 期望: 仅在 Phase 6 (MObject 改造) 删完前的残留
```

**4. 未用的 #include**：

```bash
# 每个活跃源文件顶部 include 链与本文件实际使用符号对照
```

**5. CMakeLists.txt 残留扫描**：

```bash
grep -n "ObjectCall\|MPlayer\|WorldClient\|WorldLogin\|WorldScene\|WorldRouter\|WorldMgo\|WorldServer" CMakeLists.txt
# 期望: 无输出
```

**6. 协议消息残留扫描**：

```bash
grep -rn "ObjectCallMessages.h" Source/
# 期望: 无输出
```

**完成条件**：上面 6 项 grep 全部输出符合"期望"。

---

## 第六阶段：MObject 生命周期改造（独立子项目）


### Task 6.1: 改造 MObject::Children 为 TSharedPtr

**编辑 `/root/Mession/Source/Common/Runtime/Reflect/Reflection.h`**：

```diff
 protected:
     MObject* Outer = nullptr;
-    TVector<MObject*> Children;
+    TVector<TSharedPtr<MObject>> Children;
```

**编辑 `/root/Mession/Source/Common/Runtime/Reflect/Reflection.cpp::MObject::~MObject`**：

```cpp
MObject::~MObject()
{
    RemoveFromRoot();

    if (IDisposable* Disposable = dynamic_cast<IDisposable*>(this))
    {
        if (!Disposable->IsDisposed())
        {
            Disposable->Dispose();
        }
    }

    SetOuter(nullptr);
    GetObjectMap().erase(ObjectId);

    Children.clear();  // TSharedPtr 析构链自动 delete
}
```

**编辑 `MObject::VisitReferencedObjects`**：

```cpp
void MObject::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const
{
    if (!Visitor)
    {
        return;
    }

    for (const auto& Child : Children)
    {
        if (Child)
        {
            Visitor(Child.Get());
        }
    }
}
```

---

### Task 6.2: 定义 IDisposable

**新文件** `/root/Mession/Source/Common/Runtime/Object/IDisposable.h`：

```cpp
#pragma once

class IDisposable
{
public:
    virtual ~IDisposable() = default;

    virtual void Dispose() = 0;

protected:
    void MarkDisposed() { bDisposed = true; }
    bool IsDisposed() const { return bDisposed; }

private:
    bool bDisposed = false;
};
```

---

### Task 6.3: 改造 NewMObject

**编辑 `/root/Mession/Source/Common/Runtime/Object/Object.h`，整个替换为**：

```cpp
#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Object/IDisposable.h"

namespace
{
bool HasOuterChainContains(MObject* Start, MObject* Target)
{
    MObject* Current = Start;
    TSet<MObject*> Visited;
    while (Current != nullptr)
    {
        if (Current == Target) return true;
        if (Visited.count(Current) > 0) return false;
        Visited.insert(Current);
        Current = Current->GetOuter();
    }
    return false;
}
}

template<typename TObject, typename... TArgs>
TObject* NewMObject(MObject* Outer, const MString& Name = "", TArgs&&... Args)
{
    static_assert(std::is_base_of_v<MObject, TObject>, "TObject must derive from MObject");

    if (Outer != nullptr)
    {
        check(!HasOuterChainContains(Outer->GetOuter(), Outer));
    }

    TSharedPtr<TObject> Shared = MakeShared<TObject>(std::forward<TArgs>(Args)...);
    TObject* Object = Shared.Get();

    Object->SetClass(TObject::StaticClass());
    Object->SetName(Name);

    if (Outer)
    {
        Object->SetOuter(Outer);
    }
    else
    {
        Object->AddToRoot();
    }

    return Object;
}

template<typename TObject, typename... TArgs>
TObject* CreateDefaultSubObject(MObject* Owner, const MString& Name = "", TArgs&&... Args)
{
    TObject* Object = NewMObject<TObject>(Owner, Name, std::forward<TArgs>(Args)...);
    Object->MarkAsDefaultSubObject();
    return Object;
}
```

---

### Task 6.4: 删除 DestroyMObject

从 `Object.h` 删除 `inline void DestroyMObject(MObject* Object) { delete Object; }`。

**全工程 24 处调用清理清单**（按文件）：

| 文件 | 行 | 调用上下文 | outer 关系 | 替代方案 |
|------|----|----------|----------|---------|
| `Source/Tools/MObjectAssetSmokeTool.cpp` | 156, 166, 173, 184, 192, 200, 227 | `NewMObject<MMonsterManager>(nullptr, "AssetSmokeManager")`（行 151 outer=nullptr，文件结束即销毁） | Manager 顶层持有 LoadedRoot/Monster | 改为 `TSharedPtr<MMonsterManager>` 栈上持有；LoadedRoot/Monster 由 Manager 持有 Children；末尾显式 `Manager.Reset()` 触发链式析构 |
| `Source/Servers/Scene/Combat/MonsterManager.cpp` | 121 | `MMonster* Monster = ...; DestroyMObject(Monster)`，Monster 由 Manager 持有 | Manager 作为 outer | Monster 在 Children 内，由 Manager 析构时自动 delete |
| `Source/Servers/World/Player/PlayerManager.cpp` | 121, 160 | `DestroyMObject(Player)`，Player 由 PlayerManager 持有 | PlayerManager 作为 outer | Player 在 PlayerManager.Children 内，由其析构时自动 delete |
| `Source/Tools/MObjectEditorService/Export/AssetExportService.cpp` | 126, 134, 145, 154, 165, 171, 174, 180, 191, 197 | `MMonsterConfig* RuntimeConfig` 无 outer，`LoadedObject` 由 nullptr 作 outer | 这两个对象**没有 outer**（top-level 实例） | 改为 `TSharedPtr<MMonsterConfig>`/`TSharedPtr<MObject>`；用作用域块 `{ ... }` 让 RAII 析构 |
| `Source/Tools/MObjectEditorService/Documents/MonsterConfigDocument.cpp` | 100, 109, 145 | 与上面同类：RuntimeConfig / LoadedObject 无 outer | top-level | `TSharedPtr<...>` + 作用域块 |

**关键：outer 关系检查**

| 调用点 | outer | 风险评估 |
|--------|------|---------|
| `MMonsterManager* Manager = NewMObject<...>(nullptr, "AssetSmokeManager")` (Assetsmoke 行 151) | nullptr → 进 RootSet | OK，进程结束清零；但要让 LoadedRoot/Monster 也挂到 Manager.Children 下，而不是默认 outer=nullptr |
| `MMonsterConfig* RuntimeConfig` from `BuildRuntimeObject` | 取决于 `BuildRuntimeObject` 实现；需追查 | **风险**：如果 `BuildRuntimeObject` 内部不挂 outer，RuntimeConfig 立即进 RootSet；此时改为 `TSharedPtr` 即可 |
| `MObjectAssetLoader::LoadFromBytes(bytes, Manager, &error)` (Assetsmoke 行 152) | Manager 作为 outer | 已在 Manager 下，OK |
| `MObject* LoadedObject = MObjectAssetJson::ImportAssetObjectFromJson(text, nullptr, &error)` (Documents 行 91, AssetExport 行 162) | nullptr | top-level，需 `TSharedPtr` + 块作用域 |

**清理替换模板**：

```cpp
// Before：
MMonsterConfig* RuntimeConfig = nullptr;
// ... 用 RuntimeConfig ...
DestroyMObject(RuntimeConfig);

// After：
{
    TSharedPtr<MMonsterConfig> RuntimeConfigPtr;
    // ... 用 RuntimeConfigPtr.Get() 或 *RuntimeConfigPtr ...
    // 块结束 RAII 释放
}
```

或更紧凑：

```cpp
TSharedPtr<MMonsterConfig> RuntimeConfig = MakeShared<MMonsterConfig>(/*...args...*/);
// 不再调用 DestroyMObject；natural scope end 会触发析构
```

**验证**：

```bash
cd /root/Mession
grep -rn "DestroyMObject" Source/
# 期望: 无输出（仅 Object.h 定义点也已删）
```

---

### Task 6.5: 改造 Root 集

`/root/Mession/Source/Common/Runtime/Reflect/Reflection.h`：

```diff
-    inline static TSet<MObject*>& GetRootSet()
+    inline static TSet<TSharedPtr<MObject>>& GetRootSet()
     {
-        static TSet<MObject*> RootSet;
+        static TSet<TSharedPtr<MObject>> RootSet;
         return RootSet;
     }
```

---

### Task 6.6: 编译验证

```bash
cd /root/Mession
cmake --build Build -j4
python3 Scripts/start_service.py start
sleep 2
python3 Scripts/start_service.py status
python3 Scripts/start_service.py stop
```

---

## 已知缺口与 PoC 范围声明（v2）

以下项**不计入 M5 通过门槛**，是后续阶段工作：

| 缺口 | 影响 | 后续阶段 |
|------|------|---------|
| ActorId 路由表静态配置（无控制面同步） | 进程 B 重启后其它进程路由表 stale | 接 Router/控制面服务（参考 `RefactorArchitectureAndRpc.md` §3） |
| `MClientManifest.generated.h` 当前是空 stub | 不影响 v2（v2 用 `ClientFunctionRoute` 替代） | 后续 MHeaderTool 接入 manifest |
| `IDisposable` 改造范围仅 MObject 派生类 | 旧 MObjectAssetSmokeTool/EditorService 等子树的清理 | 单独 Task 6 系列（已独立排期） |

## 验收标准（v2）

### PoC 阶段（Task 1~5）
- [ ] `rm -rf Source/Servers/World` + CMakeLists 删除 WorldServer target（Task 1.1）
- [ ] 删 `ObjectCallRouter/Registry/ObjectCall.h` + MISSION_COMMON_SOURCES 同步清（Task 1.2）
- [ ] CMakeLists 移除 4 个旧 Server target + 旧 Server 源码目录（Task 1.4）
- [ ] `MServiceContainer` 单例 + EchoService 通过它注册/解析 peer（Task 2.2）
- [ ] `EchoServiceMain` 平铺入口：`ParsePort/ParseService/ParsePeers` 一个 main 里全装配（Task 2.5）
- [ ] Gateway `ClientFunctionRoute` 查表 + `MServiceContainer::Resolve` + `CallServerFunction` 直转（Task 3.2）
- [ ] PoC 启动脚本能起 3 个进程（Gateway + Echo (+ SampleB 第 2 步扩展)）+ 每进程的 `ServiceContainer: registered peer` 日志可见（Task 4.2）
- [ ] PoC 跑通后执行 Task 5.0 代码 cleanup，6 项 grep 全部符合"期望"

### MObject 改造阶段（Task 6）
- [ ] MObject 改造编译通过
- [ ] PoC 行为不变
- [ ] `DestroyMObject` 调用点全部清除（具体清单见 Task 6.4）

---

## 依赖关系（v2）

```
Task 0.1, 0.2 (已完成)
       │
       ▼
Task 1.0 (ServiceId/InstId 位布局工具) → 1.1 (删 WorldServer) → 1.2 (删 ObjectCall*) → 1.3 (编译) → 1.4 (删 4 服)
                                                                  │
Task 2.1 (FSampleEcho* 协议)                                  │
   └→ Task 2.2 (ServiceContainer)                              │
Task 2.3 (EchoService.h)                                    │
Task 2.4 (EchoService.cpp)                                  │
Task 2.5 (EchoServiceMain + 平铺 main)                       │
                ┌─────┴─────┐
                ▼             ▼
Task 2.6 (CMake)        Task 3.1 (ClientFunctionRoute)
                │             │
                ▼             ▼
        Task 2.7 (编译)  Task 3.2 (Gateway DispatchClientCall)
                          Task 3.3 (Gateway 启动注册 peer)
                          Task 3.4 (编译)
                              │
                              ▼
                        Task 4.1 (start_service.py)
                              │
                              ▼
                        Task 4.2 (验收)
                              │
                              ▼
                        Task 5.0 (cleanup)
                              │
                              ▼
                       验收（PoC 完成）

Task 6.* (MObject 改造) 独立排期
```

---

## 进度追踪

每完成一个 Task 勾选对应 checkbox。本计划对应 `RefactorArchitectureAndRpc.md` §8 验收里程碑 M1~M6（M1 已完成，M2~M6 按 v2 重设计执行）。
