# Mession 整体架构重构方案

## 总目标

这份文档定义的是 Mession 当前阶段的目标架构，而不是一个“里程碑式”的过渡计划。

目标是把整个工程一次性收敛到下面这套设计上：

- `Server` 是进程宿主
- `Service` 是入站 RPC 服务
- `Rpc` 是出站 RPC 代理
- `Target` 只表示发送目标
- `MObject` 是唯一对象基类
- 反射、GC、异步、持久化、复制都统一收敛到同一套运行时模型

## 当前设计存在的核心问题

当前实现和预期架构之间已经出现了比较明显的偏差，主要体现在下面几个方面：

- `MFUNCTION(ServerCall, Target=...)` 里的 `Target` 目前被当成“归属服务器”，而不是“发送目标”
- `Server` 类同时承担了进程宿主、入站 RPC 入口、出站调用者、业务协调器等多重职责
- 业务代码还在手写调用胶水，比如 `CallServerFunction(...)`、`ServiceContracts`、字符串函数名
- 出站依赖需要手工组装，例如 `SWorldPlayerServiceDeps`
- 玩家运行时状态还大量依赖松散结构体，而不是对象图
- 协议层在部分地方仍然存在手写序列化风格
- 代码生成器只生成了一半框架能力，剩下一半还靠业务代码手工桥接

这些问题本质上都来自同一个原因：进程宿主、入站服务、出站代理、领域对象、协议定义、运行时胶水，还没有被彻底拆开。

## 目标架构总览

最终架构分成 6 个明确层次：

### 1. Common/Runtime

这一层负责统一运行时模型：

- `MObject`
- 反射元数据
- GC 与对象生命周期
- 异步 `Future/Promise/Task`
- 基础容器与工具
- 持久化元数据
- 复制元数据

这里必须只有一个对象体系根：`MObject`。

### 2. Common/Net

这一层只负责网络与 RPC 运行时：

- 连接管理
- 包构建与解析
- 请求/响应跟踪
- 入站分发
- 延迟响应
- 端点注册

这一层不能承载业务语义。

### 3. Protocol

这一层只放共享数据契约：

- 请求/响应结构体
- 公共枚举
- 共享错误结构
- RPC 协议层声明

这一层不能依赖具体服务器实现。

### 4. Servers/*

每个服务器进程类只做宿主：

- 启动与关闭
- 监听与连接拥有
- 运行时上下文持有
- Service 对象持有
- Rpc 代理对象持有

例如：

- `MWorldServer`
- `MGatewayServer`
- `MLoginServer`
- `MSceneServer`
- `MRouterServer`
- `MMgoServer`

### 5. Servers/*/Services

这一层定义入站服务对象：

- “别人调我”
- 请求校验
- 业务编排
- 协调领域对象和出站 Rpc

### 6. Servers/*/Rpc

这一层定义出站代理对象：

- “我调别人”
- 发送目标元数据
- 请求序列化
- 响应 future 挂接
- 对接 transport/runtime

## 统一类型模型

最终反射系统只保留下面几种核心类型：

- `MCLASS(Type=Object)`
- `MCLASS(Type=Server)`
- `MCLASS(Type=Service)`
- `MCLASS(Type=Rpc)`
- `MSTRUCT()`

规则如下：

- `MObject` 是唯一对象基类
- 不再保留 `MReflectObject` 这类并行继承链
- GC、反射、持久化、复制全部直接基于 `MObject`
- `MFUNCTION` 元数据必须区分入站与出站语义

## 正确的 RPC 语义

### 入站 Service

入站服务函数不应该带 `Target`。

示例：

```cpp
MCLASS(Type=Service)
class MWorldPlayerService : public MObject
{
    MGENERATED_BODY(...)
public:
    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> PlayerEnterWorld(
        const FPlayerEnterWorldRequest& Request);
};
```

含义：

- 这个函数可以被远端调用
- 它是一个入站 RPC 入口
- 它属于所在的 Service 类

### 出站 Rpc 代理

出站代理函数才应该带 `Target`。

示例：

```cpp
MCLASS(Type=Rpc)
class MSceneRpc : public MObject
{
    MGENERATED_BODY(...)
public:
    MFUNCTION(ServerCall, Target=Scene)
    MFuture<TResult<FSceneEnterResponse, FAppError>> EnterScene(
        const FSceneEnterRequest& Request);
};
```

含义：

- 这个函数会主动向远端发送请求
- `Target` 表示发送目标服务器
- `Target` 只能是出站语义

### 必须纠正的语义错误

当前存在这样的写法：

```cpp
MFUNCTION(ServerCall, Target=World)
MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> PlayerEnterWorld(...);
```

这和目标架构不一致，因为它把 `Target` 错用在了接收端函数上。

正确做法是：

- 入站 Service 函数移除 `Target`
- `Target` 只保留在 `Type=Rpc` 的函数上
- 服务归属由类级别或 Service 级别语义决定，而不是由函数上的 `Target` 决定

## Server 的最终组织方式

每个服务器进程最终都拆成 3 层职责：

### Host

进程宿主负责拥有运行时和对象图。

例如：

- `Servers/World/WorldServer.h/.cpp`

职责：

- 启动/关闭
- peer/backend 连接拥有
- 运行时上下文
- Service 对象拥有
- Rpc 代理对象拥有

### Services

入站 RPC 入口类。

例如：

- `Servers/World/Services/WorldPlayerService.h/.cpp`

职责：

- 暴露 World 的入站玩家接口
- 校验请求
- 编排业务流程
- 调用领域对象和出站 Rpc

### Rpc 代理

出站调用类。

例如：

- `Servers/World/Rpc/LoginRpc.h/.cpp`
- `Servers/World/Rpc/MgoRpc.h/.cpp`
- `Servers/World/Rpc/SceneRpc.h/.cpp`
- `Servers/World/Rpc/RouterRpc.h/.cpp`

职责：

- 向远端服务器发请求
- 对业务隐藏 transport 细节
- 返回强类型 future

## 目标业务调用方式

最终业务层应该是这种写法：

```cpp
MFuture<TResult<FPlayerEnterWorldResponse, FAppError>>
MWorldPlayerService::PlayerEnterWorld(const FPlayerEnterWorldRequest& Request)
{
    MSceneRpc* SceneRpc = GetSceneRpc();
    if (!SceneRpc)
    {
        return MakeErrorFuture(...);
    }

    auto SceneResult = SceneRpc->EnterScene(...);
    ...
}
```

它要替代掉当前这类写法：

- 手工组 `SWorldPlayerServiceDeps`
- 手工传 `ServiceContracts`
- 手工调用 `CallServerFunction(...)`
- 手工传字符串函数名

## 对代码生成器的要求

`MHeaderTool` 需要从“只生成部分胶水”升级成“生成完整服务/代理框架”。

它必须生成下面 4 类代码：

- Service 的入站分发表
- Rpc 代理的出站调用 stub
- Service/Rpc 的反射注册
- 函数 id、序列化、响应 future 挂接相关 glue

生成规则：

- 只有对应头文件可以 include 自己的 `.mgenerated.h`
- 不再保留游离的 Manifest 头文件
- 协议层不再出现手写 `Serialize/Deserialize`
- 协议序列化统一依赖 `MPROPERTY`
- 生成代码必须识别 `Type=Service` 和 `Type=Rpc`

## 重构后的运行时职责

### ServerRpcRuntime

重构后，`ServerRpcRuntime` 不再根据函数上的 `Target` 去推断“它属于哪个服务”。

它只负责：

- 入站 Service 分发
- 出站 Rpc 调用注册
- 响应完成
- 超时/断连清理
- 延迟响应支持
- transport 对接

### 出站调用解析

Rpc 代理不能再依赖业务层手工传连接，必须从统一运行时上下文里拿目标 transport。

最终必须引入统一抽象，例如：

- `IServerRuntimeContext`
- `IRpcTransportResolver`

这一层提供：

- 按 `EServerType` 查找连接
- 判断目标服务是否可用
- 发送请求
- 注册请求并完成 future

## ServiceContracts 的最终定位

`ServiceContracts.h` 是当前的过渡层，不是最终态。

最终原则：

- 业务代码不能直接依赖 `ServiceContracts`
- 业务代码不能传 endpoint class name
- 业务代码不能传字符串函数名

当前写法：

```cpp
CallServerFunction(..., *Deps.SceneService, "EnterScene", Request)
```

最终要变成：

```cpp
SceneRpc->EnterScene(Request)
```

`ServiceContracts` 如果还需要存在，也只能作为框架内部注册信息，不再暴露给业务层。

## 领域对象模型

玩家/玩法层必须正式迁移到 `MObject` 对象模型上。

目标对象图：

- `MPlayerSession : MObject`
- `MPlayerAvatar : MObject`
- `MInventoryComponent : MObject`
- `MAttributeComponent : MObject`

规则：

- Session 和 Avatar 必须是对象，而不是松散结构体
- 在线玩家注册表最终应该引用对象，而不是碎片状态
- Gameplay 组件应该作为子对象存在
- 对象之间的关系必须显式可遍历

## 对象生命周期与 GC

这次重构必须把对象创建和生命周期正式定下来：

- `NewMObject<T>(...)`
- `CreateDefaultSubObject<T>(...)`
- RootSet
- 子对象跟踪
- 引用遍历
- GC，或者至少先有统一对象注册与受控销毁

规则：

- 不允许直接 `new MPlayerAvatar()`
- 组件必须通过 subobject 接口创建
- 反射和生命周期必须用同一套对象系统

## Persistence 与 Replication 的最终接入方式

前面已经打了一些基础，但还没有真正和对象系统完全合流。

最终要求：

- `MPROPERTY` 决定持久化字段
- `MPROPERTY` 决定复制字段
- dirty domain 和 dirty property 记录在对象层
- persistence sink 只消费对象变更
- replication 只消费对象/domain 变更

业务代码不能再做下面这些事：

- 字段一改就散写 DB
- 手工拼同步包

Mongo 持久化最终应该是“按属性展开”的结构，而不是以二进制快照为主结构。

## Protocol 层清理目标

Protocol 层最终只承载“数据契约”。

不应该继续存在：

- 手写 `Serialize/Deserialize`
- 协议结构体和 transport 细节混在一起
- 按 `Client*` / `Server*` 二分的大杂烩消息头

建议目录形式：

- `Protocol/Messages/Auth/*`
- `Protocol/Messages/World/*`
- `Protocol/Messages/Scene/*`
- `Protocol/Messages/Router/*`
- `Protocol/Messages/Mgo/*`
- `Protocol/Messages/Common/*`

协议模块应该按业务域拆分，而不是只按“谁发给谁”拆分。

## Runtime 文件拆分目标

`ServerRpcRuntime` 目前还太臃肿，最终应该拆成：

- `RpcTransport.h/.cpp`
- `RpcDispatch.h/.cpp`
- `RpcClientCall.h/.cpp`
- `RpcServerCall.h/.cpp`
- `RpcManifest.h/.cpp`
- `RpcErrors.h`

命名规则：

- 面向运行时的 API 不再带 `Generated`
- 只有真正的代码生成器内部边界，才允许保留生成语义

## 目标目录结构

最终目录必须收敛到下面这种结构：

- `Source/Common/Runtime/Object/*`
- `Source/Common/Runtime/Reflect/*`
- `Source/Common/Runtime/Concurrency/*`
- `Source/Common/Runtime/Persistence/*`
- `Source/Common/Runtime/Replication/*`
- `Source/Common/Net/Rpc/*`
- `Source/Common/Net/Transport/*`
- `Source/Protocol/Messages/Auth/*`
- `Source/Protocol/Messages/World/*`
- `Source/Protocol/Messages/Scene/*`
- `Source/Protocol/Messages/Router/*`
- `Source/Protocol/Messages/Mgo/*`
- `Source/Protocol/Rpc/*`
- `Source/Servers/World/WorldServer.*`
- `Source/Servers/World/Services/*`
- `Source/Servers/World/Rpc/*`
- `Source/Servers/World/Domain/*`
- `Source/Servers/Gateway/...`
- `Source/Servers/Login/...`
- `Source/Servers/Scene/...`
- `Source/Servers/Router/...`
- `Source/Servers/Mgo/...`

## 最终必须替换掉的旧模式

这次重构完成后，业务层必须彻底摆脱下面这些模式：

- 入站函数上的 `MFUNCTION(ServerCall, Target=...)`
- 业务层直接使用 `CallServerFunction(...)`
- 业务层直接使用 `ServiceContracts`
- 手工装配依赖结构，例如 `SWorldPlayerServiceDeps`
- 玩家状态主要依赖散乱的 `TMap + struct`
- 出站调用靠字符串函数名识别
- 协议层手写序列化
- `Server` 类同时充当宿主、服务、代理、领域对象

## 完整实现流程

这部分不是“阶段划分”或“里程碑列表”，而是整个工程一次性收敛到目标架构时，代码必须形成的一条完整实现链。

本章所有条目都属于硬性约束。实现可以有局部展开顺序，但不能改变语义终态，也不能保留与终态冲突的旧模型作为长期并存方案。

实现时要把下面这些环节看成同一个闭环，而不是彼此独立的子任务：

- 类型系统如何定义对象
- 代码生成器如何识别对象
- Runtime 如何调度入站与出站调用
- Server 如何只做宿主
- Service 如何编排业务
- Rpc 如何向外发请求
- Domain Object 如何承载状态
- Persistence/Replication 如何消费对象变更
- Protocol 如何只保留数据契约

只要其中任何一环仍然保留旧模型，整体架构就没有真正完成。

### 统一语义先落到底层

整个流程的起点不是某个服务器，而是底层语义收口。

必须先把反射、代码生成、运行时都统一到同一套定义上：

- `MCLASS(Type=Server)` 只表示进程宿主
- `MCLASS(Type=Service)` 只表示入站 RPC 服务
- `MCLASS(Type=Rpc)` 只表示出站 RPC 代理
- `MCLASS(Type=Object)` 承载领域对象
- `MSTRUCT()` 只承载数据契约

`MFUNCTION` 的语义也必须在这一层一次性定死：

- 入站函数是 `Service` 上的 `MFUNCTION(ServerCall)`
- 出站函数是 `Rpc` 上的 `MFUNCTION(ServerCall, Target=...)`
- `Target` 永远不再表示“归属服务器”
- 函数 owner、函数 target、调用方向都必须在元数据层清晰可区分

`MHeaderTool` 和反射注册代码必须直接基于这套语义生成完整 glue，而不是再依赖业务层手工补桥：

- 为 `Service` 生成入站分发表
- 为 `Rpc` 生成出站 stub
- 生成函数 id 与参数序列化 glue
- 生成 future 响应挂接 glue
- 生成 `Service` / `Rpc` / `Object` 的反射注册信息

到这一步为止，框架层必须已经具备一个能力：业务层即使完全不写 `CallServerFunction(...)`，也仍然能完成完整的服务调用链。

### Runtime 直接围绕 Service / Rpc 工作

当底层语义固定后，`ServerRpcRuntime` 的职责就不再是“根据函数 Target 猜这个函数属于谁”，而是围绕明确的 Service/Rpc 对象模型工作。

这一层必须形成下面这套运行方式：

- 入站请求进入 Runtime
- Runtime 根据函数 id 找到目标 `Service`
- Runtime 负责参数解析、调用上下文、延迟响应、异常保护
- 出站 `Rpc` 代理通过统一 transport resolver 找到目标连接
- Runtime 负责请求登记、超时、断连清理、future 完成

业务代码在这一步之后不再接触这些细节：

- 连接选择
- endpoint class name
- 字符串函数名
- 手工请求 id 跟踪
- 手工 future completion

如果某一处业务代码仍然需要自己决定“发到哪个 class、调用哪个字符串函数名”，就说明 Runtime 仍然没有真正收口。

### Server 只保留宿主职责

当 Runtime 可以直接服务于 `Service/Rpc` 之后，每个服务器进程类就必须退回到宿主身份。

每个 `Server` 最终都只做下面这些事情：

- 启动和关闭进程
- 拥有监听 socket 与 backend 连接
- 拥有 runtime context
- 持有本机 `Service`
- 持有本机出站 `Rpc`
- 持有本机领域对象根与注册表

它不再自己承担这些职责：

- 直接实现入站业务 RPC
- 手工拼出站调用参数
- 兼任领域对象
- 直接保存碎片状态并参与玩法逻辑

也就是说，`Server` 的代码看起来应该更像装配器和宿主，而不是业务控制器。

### 所有入站能力都进入 Service

接下来，所有“别人调我”的入口都必须集中到 `Services/*` 下的对象里。

这些 `Service` 的职责是：

- 接收强类型 request
- 做请求合法性校验
- 查找或创建领域对象
- 编排本地状态变更
- 调用出站 `Rpc`
- 返回强类型 `MFuture<TResult<...>>`

这里最关键的不是“把函数换个文件放”，而是把业务入口从宿主类抽离成可组合的对象。

因此最终状态必须满足：

- `LoginServer` 不再自己实现 `IssueSession`
- `WorldServer` 不再自己实现 `PlayerEnterWorld`
- `SceneServer` 不再自己实现 `EnterScene`
- `RouterServer` 不再自己实现 `ResolvePlayerRoute`
- `MgoServer` 不再自己实现 `LoadPlayer`

这些能力都属于对应的 `Service` 对象。

### 所有出站能力都进入 Rpc

与入站相对，所有“我调别人”的能力都必须进入 `Rpc` 对象。

每个 `Rpc` 类必须只做一件事：向某类目标服务器发起某一组强类型调用。

例如 World 侧最终应该拥有：

- `MWorldLoginRpc`
- `MWorldMgoRpc`
- `MWorldSceneRpc`
- `MWorldRouterRpc`

Gateway 侧也应该拥有自己的出站代理，而不是继续通过业务 flow 手工组装调用依赖。

最终业务层看到的调用方式只能是：

```cpp
LoginRpc->ValidateSessionCall(Request);
SceneRpc->EnterScene(Request);
RouterRpc->UpsertPlayerRoute(Request);
```

而不能再出现：

```cpp
CallServerFunction(..., "EnterScene", ...);
```

也不能再通过依赖结构体去传：

- `TSharedPtr<MServerConnection>`
- `SServerServiceContract*`
- endpoint class name
- 字符串函数名

如果还要传这些，说明 `Rpc` 还只是“语法糖”，并没有真正承担代理职责。

### World 业务流必须直接建立对象图

当前工程里，最重要的不是先把所有服务器都写成同一外形，而是让第一条完整业务链真的落到对象系统上。

这条链应该从 World 开始，并且在进入 World 时直接建立领域对象图。

目标对象关系应当是：

- `MPlayerSession`
- `MPlayerAvatar`
- `MInventoryComponent`
- `MAttributeComponent`

其中：

- `MPlayerSession` 表示在线会话和路由态
- `MPlayerAvatar` 表示玩家角色运行态
- Inventory / Attribute 等玩法组件作为 Avatar 的子对象存在

World 的在线玩家注册表最终引用的应该是对象，而不是 `TMap<uint64, SWorldPlayerState>` 这样的碎片状态。

也就是说，World 的完整业务流应该是这样：

- Gateway 发起玩家登录
- Gateway 的 `Rpc` 调用 Login
- Login 返回 session 信息
- Gateway 的 `Rpc` 调用 World 的入站 `Service`
- World 的 `Service` 校验 session
- World 的 `Service` 通过 `MgoRpc` 加载玩家数据
- World 创建或恢复 `MPlayerSession`
- World 创建或恢复 `MPlayerAvatar`
- Avatar 在构造阶段创建自己的默认子对象，例如 Inventory / Attribute
- World 通过 `SceneRpc` / `RouterRpc` 完成场景与路由编排
- World 把在线玩家注册表指向该对象图

从这一步开始，玩家在线态、玩法组件、脏标记、持久化、复制，都必须以对象图为中心展开。

### 对象创建、Subobject 与 GC 要用同一套模型

对象图建立之后，生命周期就不能继续靠业务代码手工管理。

必须统一到下面这套模型：

- `NewMObject<T>(Outer, ...)`
- `CreateDefaultSubObject<T>(Owner, Name)`
- RootSet
- 子对象跟踪
- 引用遍历
- 统一受控销毁
- 最终接入 GC

规则必须是强约束，而不是建议：

- 不允许业务直接 `new MPlayerSession()`
- 不允许业务直接 `new MInventoryComponent()`
- 组件只能通过 subobject 接口创建
- 对象关系必须能通过 `Outer + Children + 引用遍历` 找全

GC 在实现形式上可以逐步增强，但对象模型不能是半套的。

也就是说，哪怕初期只是“统一对象注册 + 标准销毁 + 根集遍历”，其接口和数据结构也必须已经是最终 GC 体系的一部分，而不能以后再推翻重来。

### 脏标记、Persistence、Replication 全都回到对象层

对象系统一旦成立，持久化和复制都必须停止直接消费业务层的散乱状态。

最终模型必须是：

- `MPROPERTY` 决定哪些字段可持久化
- `MPROPERTY` 决定哪些字段可复制
- 对象在属性变更时统一记录 dirty property
- 对象在属性变更时统一记录 dirty domain
- persistence sink 只消费对象变更
- replication driver 只消费对象和属性变更

业务代码不能再继续做这些事：

- 属性一改立刻自己写 DB
- 在业务逻辑里手工拼同步消息
- 在多个系统里分别维护“哪个字段脏了”

World 中对象图建立之后，正确的数据流应该变成：

- `Service` 或领域对象修改 `MPROPERTY`
- 对象层记录 dirty property / dirty domain
- persistence subsystem 在统一位置收集对象变更
- replication subsystem 在统一位置收集对象变更
- 两者都直接基于对象元数据导出内容

Mongo 落库在最终形态上也应该围绕“对象属性展开”组织，而不是继续依赖整块二进制快照。

### Protocol 只保留数据契约

当业务和状态都已经回到对象层后，Protocol 层必须进一步清理成纯数据契约层。

这里的原则非常简单：

- Protocol 定义结构
- Runtime 决定传输
- Service/Rpc 决定语义
- 业务代码决定编排

Protocol 本身不应该继续混入：

- transport 细节
- 手写 `Serialize/Deserialize`
- 按 `Client*` / `Server*` 粗暴分堆的消息头

最终目录应按业务域组织：

- `Protocol/Messages/Auth/*`
- `Protocol/Messages/World/*`
- `Protocol/Messages/Scene/*`
- `Protocol/Messages/Router/*`
- `Protocol/Messages/Mgo/*`
- `Protocol/Messages/Common/*`

所有 request / response / payload 都应该通过 `MSTRUCT + MPROPERTY` 进入统一反射与序列化体系。

只要某个协议类型还要求手工实现序列化函数，就说明协议层仍然没有真正接入最终模型。

### Runtime 最终拆分成稳定边界

当前的 `ServerRpcRuntime` 仍然承担过多职责，所以当上面的语义、对象图、协议清理全部收口后，Runtime 文件边界也必须同步收口。

最终应该拆成下面这些稳定模块：

- `RpcTransport`
- `RpcDispatch`
- `RpcClientCall`
- `RpcServerCall`
- `RpcManifest`
- `RpcErrors`

它们各自只做自己的边界内工作：

- `RpcTransport` 负责发送和连接抽象
- `RpcDispatch` 负责入站分发
- `RpcClientCall` 负责出站请求登记、future、超时
- `RpcServerCall` 负责服务端调用上下文与响应
- `RpcManifest` 负责反射/注册/函数清单
- `RpcErrors` 负责错误定义与错误编码

这一步完成后，面向运行时的公共 API 不应该再带有“Generated”语义命名；生成器只对真正的生成边界负责。

## 实施约束

为了避免实现再次滑回“过渡方案”，这里把整份文档的执行约束单独列出来。

- 不允许新增任何继续依赖 `CallServerFunction(...)` 的业务调用点
- 不允许新增任何继续依赖 `ServiceContracts` 的业务层代码
- 不允许新增任何入站 `MFUNCTION(ServerCall, Target=...)`
- 不允许新增任何以字符串函数名作为主要识别方式的业务调用
- 不允许新增任何以 `TMap + struct` 作为长期玩家主状态模型的实现
- 不允许在领域对象之外分散维护脏标记、持久化状态、复制状态
- 不允许让 `Server` 类重新承担 Service、Rpc、Domain Object 的职责
- 不允许让 Protocol 层继续承担 transport 或手写序列化职责

如果某次实现为了短期联调暂时保留了兼容层，这些兼容层也必须满足两个条件：

- 只能存在于框架内部边界
- 不能重新暴露给业务层

## 完成判定

这次重构不是“看起来差不多”就算完成，而是必须同时满足架构判定和工程判定。

### 架构完成判定

当这份文档真正落地时，整个工程应该同时满足下面这些条件：

- 所有服务器进程类都只做宿主
- 所有入站 RPC 都属于 `Service`
- 所有出站调用都属于 `Rpc`
- `Target` 只表示发送目标
- 玩家状态与玩法组件都已经进入 `MObject` 对象图
- 对象创建、subobject、生命周期、GC 使用同一套模型
- 持久化和复制都直接消费对象变更
- Protocol 层只保留数据契约
- Runtime 按职责拆分完成
- 业务层不再直接依赖 `ServiceContracts`
- 业务层不再直接使用 `CallServerFunction(...)`
- 业务层不再依赖手工字符串函数名
- 业务层不再以 `TMap + struct` 作为主要状态模型

如果最终代码还保留这些旧模式中的任意一项，就说明重构仍然没有真正完成：

- 入站函数上的 `MFUNCTION(ServerCall, Target=...)`
- 业务层直接调用 `CallServerFunction(...)`
- 业务层直接依赖 `ServiceContracts`
- 手工装配依赖结构，例如 `SWorldPlayerServiceDeps`
- 玩家状态主要依赖散乱的 `TMap + struct`
- 出站调用靠字符串函数名识别
- 协议层手写序列化
- `Server` 类同时充当宿主、服务、代理、领域对象

### 工程完成判定

除了结构完成以外，还必须同时满足下面这些工程判定：

- 全量编译通过
- 代码生成结果稳定且可重复生成
- integration validate 通过
- Mongo sandbox persistence 链路可用
- 并发场景下没有新增明显竞态或对象生命周期错误
- 请求超时、断线、延迟响应三类路径都能正确完成 future 与清理上下文
- 登录、进世界、切场景、登出、路由更新这几条主链路都能在新模型下闭环

只要上面任意一项不成立，这次重构就仍然只能算“部分完成”，不能算架构收口完成。

## 最终判断

当前的问题并不是单一命名问题，而是下面这几层还没有彻底分开：

- 进程宿主
- 入站服务
- 出站代理
- 领域对象
- 协议
- 运行时胶水

这次重构的目标，就是彻底把这些职责拆开，并最终收敛到“透明 RPC、对象中心、反射驱动”的分布式游戏服务器架构上。
