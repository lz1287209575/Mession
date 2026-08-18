# Player 设计规格(2026-08-14)

> 状态:设计定稿,目标架构 = Actor/ActorMember(协议下沉到成员)。
> 上游决策:`2026-08-14-player-session-db-design.md`(决策主档,入口)
> + `2026-08-14-actor-member-framework.md`(F0 框架设计)。

## 1. 分层总览

```
┌─ LoginService(认证/会话进程)——信任边界,与 PlayerService 同构:零业务协议
│    └─ MLoginAuth (Type=ActorMember)认证成员,宿主 = MLoginService 服务实例(无 actor 容器)
│       Login/Logout(认证/建号/选实例/踢旧)
├─ MPlayerService(进程,×N 同质池)——零业务协议:只持 actor 集合 + 框架 member 分发器
├─ MPlayerActor(每玩家,Type=Actor)——actor 容器:自身无协议,业务在 ActorMember 上
│    ├─ MPlayerLogin        (Type=ActorMember)登录/会话激活成员
│    ├─ MPlayerStats    (Type=ActorMember)基础属性成员
│    ├─ MPlayerItemContainer(Type=ActorMember)背包成员
│    └─ MPlayerQuest        (Type=ActorMember)任务成员
└─ Global actors(仿 MRankListActor)——排行榜/邮件/全服广播(跨玩家,单实例)
```

- EchoService 是 PoC 验证桩,不承载 Player(§2.2)。
- 服务不拆(§2.2);进程内按 actor 形态组织(per-player / global)。
- **Login 不在 PlayerService**(§2.2):认证是信任边界,独立 LoginService 承载。
- **协议下沉到 ActorMember**(目标架构):Service 类零业务协议,
  每个业务模块 = 一个 `MCLASS(Type=ActorMember)` 类,自带 `MFUNCTION(ServerCall)`。

### 1.1 服务间通信(走 ServiceDiscovery 层)

**原则:服务间不硬编码对端地址、不经 Gateway 中转业务编排——经 Registry 发现
(`MEndpointCache`)+ 语义寻址(`EServerType`)+ 服务间 RPC(`MRpc::CallRemote`)**。

| 调用 | 通道 | 说明 |
|---|---|---|
| 客户端 → LoginService(Login/Logout) | Gateway 转发(server RPC) | 客户端协议,Gateway 按 FunctionId+Target 路由 |
| 客户端 → MPlayerService(业务) | Gateway 转发(server RPC) | 同上 |
| LoginService → MPlayerService(EnterGame/踢旧/注销) | **经 MEndpointCache 发现 + CallRemote(EServerType::Player)** | 服务间会话编排,非客户端协议 |
| MPlayerService → Gateway(下行推送) | CallClient(§4.5) | 客户端下行 |

- Gateway 职责收敛:客户端进出 + connId↔playerId 绑定 + 业务消息路由,**不参与服务间业务编排**。
- 服务间函数(EnterGame/KickPlayer/PlayerLogout)注册为服务间 RPC 入口
  (非客户端 MClientManifest 转发表条目)。
- 实例定位:PlayerId 编码 InstId(§4.1)→ 经 MEndpointCache 拿目标实例 endpoint。

**实例亲和性(同一 Player 必须命中同一实例)**——`MEndpointCache::GetOrConnect(EServerType)`
是 round-robin 不粘;用 Registry `FServiceEndpoint.ActorIds` 元数据做粘性路由:

| 环节 | 保证 |
|---|---|
| 首次选实例 | 账号 hash 稳定映射(同账号同实例);PlayerId 编码 InstId(§4.1) |
| 创建后 | PlayerActor Register → ActorId 上报 Registry(`ActorIds` 更新) |
| 后续/重连 | **按 PlayerId 查 ActorIds 定位实例**(不用 round-robin)→ 原实例,宽限期复用 |

**实现缺口(F0/P1)**:MEndpointCache 需 `FindEndpointByActorId(PlayerId)`;actor 上报链路
(MActorSystem Register → RegistryProtocol)未接——即 Active gap "Prove cross-Echo
CallToActor under Registry actor metadata"。

### 1.2 同质池实例协同与故障迁移

### 实例间协同(同一 Service 多实例)
- PlayerActor 按 PlayerId hash 分布到实例(§4.1);跨实例 actor 调用(好友/组队/Global
  actor)依赖 **跨实例 CallToActor**——PlayerId 编码 InstId → 路由层解析 InstId →
  该实例 endpoint(经 Registry `ActorIds` 元数据)。即 Active gap "Prove cross-Echo
  CallToActor under Registry actor metadata",是实例间协同的基建。
- Global actor(排行榜/邮件)固定在某实例,其余实例经上述链路调用。

### 故障迁移(实例崩溃)
**崩溃迁移 = DB 重载,不是内存迁移**——崩溃瞬间内存态消失,DB 是唯一真相:

```
1. 检测:Registry 心跳超时 → endpoint 移除(ActorIds 清空);Gateway 检测连接断
2. 玩家重连 → LoginService.Login
3. 选实例:hash 命中死实例 → fallback 重选(跳过 unhealthy,取下一健康实例)
   ——实例亲和性在崩溃时让位于可用性
4. 数据恢复:新实例创建 PlayerActor(成员挂载)→ DB 全量重载(Profile + 成员数据)
5. Gateway 绑定更新:playerId → 新实例;connId↔playerId 不变
```

**丢档窗口**(取决于落库时机):

| 落库策略 | 崩溃丢档窗口 |
|---|---|
| 登出/宽限超时落库(已定,§4.6) | 上次落库 → 崩溃之间变更全丢 |
| 定期快照(TODO 后置) | ≤ 快照间隔 |
| 关键变更即时落库 | 最小,DB 压力大 |

- PoC 接受"登出落库 + 崩溃丢窗口";定期快照(如 30s)后置缓解。
- 玩家体验:自动重连(Login → fallback → DB 重载),感知为"掉线重连"。

## 2. 进程:PlayerService(零业务协议,actor 容器)

```cpp
// Source/Servers/Player/PlayerService.h —— 用户理想形态
MCLASS(Type = Service)
class MPlayerService : public MNetServerBase, public MObject
{
public:
    MGENERATED_BODY(MPlayerService, MObject, 0)

    bool Init(int InPort = 0);
    void Run() override { MNetServerBase::Run(); }
    // MNetServerBase 子类必需(进程骨架:监听/事件循环/Registry):
    uint16 GetListenPort() const override;
    void   OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void   ShutdownConnections() override;

protected:
    TVector<MPlayerActor> PlayerActors;   // actor 集合(Service 只持有,零业务协议)
};
```

- **零业务协议**:不在 Service 类声明任何 `MFUNCTION(ServerCall)`——协议在
  ActorMember 上(§3)。进程接收 member 调用由**框架内部成员分发器**完成
  (见 `2026-08-14-actor-member-framework.md` §4.3:FunctionId → GMemberRpcEntries
  → 请求取 PlayerId → Find(PlayerId) → member 实例 → 反射调用),业务不可见,
  不是业务统一信封(信封已否决)。

## 2.1 LoginService(认证/会话进程,信任边界)——与 PlayerService 同构,无 actor 容器

**零业务协议 + ActorMember 成员;宿主 = 服务实例(单例,不建容器 actor)**——认证是
单例系统能力,无 per-player 实例化/寻址/生命周期需求,容器只是空架子(已否决):

```cpp
// Source/Servers/Login/LoginService.h —— MCLASS(Type = Service)
class MLoginService : public MNetServerBase, public MObject
{
    MGENERATED_BODY(MLoginService, MObject, 0)
    bool Init(int InPort = 0);
    void Run() override { MNetServerBase::Run(); }
    uint16 GetListenPort() const override;
    void   OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void   ShutdownConnections() override;
protected:
    // 成员宿主 = 服务实例(进程单例);框架按注册表自动实例化并挂载
    TSharedPtr<MLoginAuth> LoginAuth;
};

// Members/MLoginAuth.h —— MCLASS(Type = ActorMember):认证成员,协议在此
MCLASS(Type = ActorMember)
class MLoginAuth : public IActorMember
{
    MFUNCTION(ServerCall, Async)
    SFutureResult<FLoginResponse> Login(const FLoginRequest& Req);    // 认证/建号/选实例/踢旧裁决
    MFUNCTION(ServerCall, Async)
    SFutureResult<FEmptyResponse> Logout(const FLogoutRequest& Req);  // 会话清理
    // 内部 AWAIT DBService(查/建 Profile)
};
```

**会话逻辑归属**:
- 认证/建号/选实例/踢旧裁决/票据 → `MLoginAuth` 成员(宿主 = LoginService 实例,系统能力)。
- **成员宿主双形态(框架)**:per-player 成员挂 actor 容器;单例成员(认证)挂服务实例——
  见 actor-member-framework §4。
- 登录请求寻址:无 PlayerId → FunctionId 直发 LoginService 进程 → 服务实例成员(无容器寻址)。
- 登录成功 → 回 `LoginResponse{PlayerId, 目标 PlayerService 实例}`;
  Gateway 绑定 connId↔playerId 并通知目标实例**会话激活**(`EnterGame`),
  由 MPlayerService 创建 MPlayerActor(见 §6 时序)。
- 断线/重连/登出落库/宽限期 → PlayerActor 状态机(§4.6)。
- 踢旧由 MLoginAuth 裁决:发现旧 PlayerActor 在线 → 发 Kick 给旧连接,
  旧 actor 落库注销 → 再创建新会话。

## 3. Actor 与 ActorMember(目标架构:协议下沉到成员)

```cpp
MCLASS(Type = Actor)
class MPlayerActor : public IActor
{
    // actor 容器:自身无协议;成员由框架按注册表自动实例化并挂载(见 F0 框架设计)
    uint64 GetActorId() const override;   // = PlayerId
};

MCLASS(Type = ActorMember)
class MPlayerLogin : public IActorMember
{
    MFUNCTION(ServerCall, Async)
    SFutureResult<FPlayerLoginResponse> Login(const FPlayerLoginRequest& InRequest)
    {
        MPlayerItemContainer* ItemContainer = GetActorMember<MPlayerItemContainer>();
        // await 成员方法(TAwaitable 表达式形态;成员函数指针 await 为 F0 演进项)
        int32 Loaded = TAwaitable<&MPlayerItemContainer::LoadAllItem>(ItemContainer);
        return Ok(FPlayerLoginResponse{...});
    }
};

MCLASS(Type = ActorMember)
class MPlayerItemContainer : public IActorMember
{
    MFUNCTION(Async)
    SFutureResult LoadAllItem();

    MFUNCTION(ServerCall, Async)
    SFutureResult<FPlayerUseItemResponse> UseItem(const FPlayerUseItemRequest& InRequest);
};
```

**特征**:
- 协议分散粒度 = **ActorMember 类**(每个业务模块一个成员类,自带 MFUNCTION(ServerCall));
  Service 类零业务协议(只有骨架 + actor 集合)。
- 成员间协作:`GetActorMember<T>()` 查找 + `TAwaitable` 表达式 await
  (`int R = TAwaitable<&Member::Method>(Member, args...)`),同 actor 同线程。
- actor 实例化:框架按成员类型注册表自动 `NewMObject<T>` 创建全部成员并挂载
  (PoC 单 actor 类型;多类型演进到 Meta 显式声明)。

**框架支撑(现状差距,见 F0 框架设计)**:`EClassKind.Actor/ActorMember`、MHeaderTool
`Type=ActorMember` 生成(注册宏 + `GMemberRpcEntries`)、`IActorMember` 基类、
member 上 ServerCall 的注册与寻址(请求显式带 PlayerId)、`GetActorMember<T>()`;
member 间 await 需 `TAwaitable` 支持成员函数指针(`TAwaitableFnTraits` 特化,现仅自由函数)。
**落地路径:先框架后业务(已决策)**。

## 4. 成员组织与通信(ActorMember 模型)

- 成员 = `MCLASS(Type=ActorMember)` + `IActorMember` 基类(含 MObject 反射身份,
  见 F0 框架设计)——不是 TSharedPtr 组件,是框架级概念。
- 成员生命周期:actor 创建时挂载(`AttachToActor`/`OnAttach`),actor 注销时卸载。
- 成员间通信(同 actor 同线程,零锁):
  1. **`GetActorMember<T>()` 查找 + 直接调用**(读数据、基础能力);
  2. **`TAwaitable` 表达式 await 成员方法**(`int R = TAwaitable<&Member::Method>(Member, args...)`;
     F0 演进项:TAwaitableFnTraits 需支持成员函数指针);
  3. 跨成员业务流由**调用方成员编排**(如 Login 成员调背包成员 LoadAllItem),
     不经 actor 中转。
- 依赖方向避免环(如 Login → ItemContainer → Attribute)。
- 成员数据:成员自带 Schema(§5),`Save()/Load()` 由 actor 数据生命周期驱动。

## 5. 数据模型(Schema,DBService 集合)

```cpp
// Source/Protocol/Messages/Player/FPlayerDataSchemas.h
MSTRUCT() struct FPlayerProfile { /* §4.2:Account/PasswordHash/DisplayName/Level/Exp/时间戳 */ };
MSTRUCT() struct FInventoryData { MPROPERTY() TVector<FItem> Items; };        // inventory 集合
MSTRUCT() struct FQuestData    { MPROPERTY() TMap<uint64, FQuestState> Quests; }; // quests 集合
```

- 每成员一个 Schema/集合(成员自治);登录并行 Load,登出/宽限超时 Save。
- 落库格式遵循设计文档 §3.4(标量→列 / 嵌套→JSON 子列或二进制,按字段可配)。
- 版本化铁律 §3.5(字段只增不删,Deprecated 标记)。

## 6. 生命周期时序

**登录**(跨进程,§4.4;会话编排走 ServiceDiscovery 层):
```
客户端 → Gateway(按 FunctionId+Target 路由)→ LoginService 进程
  → 框架 member 分发器:FunctionId(Login)→ 服务实例成员 MLoginAuth(无容器寻址)
  → MLoginAuth(认证成员,单线程):
      1. 踢旧裁决(§4.6):旧会话所在 Player 实例 → 经 MEndpointCache 发现
         → CallRemote(EServerType::Player, "KickPlayer", {PlayerId}) → 旧 actor 落库注销
      2. 认证/建号(DBService 查/建 Profile)+ 选实例 → PlayerId(§4.1,编码 InstId)
      3. 回包 LoginResponse{PlayerId, 目标 PlayerService 实例}
Gateway:绑定 connId↔playerId + 记录 playerId→实例(仅路由,不参与业务编排)
  → [会话激活] LoginService 经 MEndpointCache 发现目标 Player 实例
      → CallRemote(EServerType::Player, "EnterGame", {PlayerId})(服务间 RPC,非客户端协议)
  → MPlayerService:创建 MPlayerActor(成员自动挂载)+ Register
      + OnLogin(查 Profile / 成员并行 Load)
  → 回 LoginResponse → CallClient 下行欢迎 + View(经 Gateway)
```

**登出 / 断线 / 踢下线**(§4.6;登出经 LoginService 走 ServiceDiscovery 层):
```
登出:   客户端 → Gateway → LoginService.Logout → LoginService 经发现层
       CallRemote(EServerType::Player, "PlayerLogout", {PlayerId})
       → PlayerActor.OnLogout → 成员 Save(DBService,Upsert 成功)
       → Unregister(PlayerId) → Gateway 解绑
断线:   Gateway 检测 → 解绑 → actor.OnDisconnected → Reconnecting(60s)
       ├─ 期内重连:回原实例 → OnReconnected(复用内存态) → Gateway 换绑新 ConnId
       └─ 超时:成员 Save → 落库 → Unregister
踢下线: 重复登录 → LoginService 经发现层 CallRemote("KickPlayer") → 旧连接 Kick → OnKicked(Save + Unregister)
```

**业务**(member 协议直发):
```
客户端 → Gateway(按 FunctionId + Target 路由)→ MPlayerService 进程
  → 框架 member 分发器:FunctionId → GMemberRpcEntries → 请求取 PlayerId
  → Find(PlayerId) → actor → member 实例 → 反射调用 → 回包
下行:成员变更 → actor 判定需同步 → Gateway 绑定表 → CallClient(§4.5)
```

### 6.1 响应收束与下行闭环(收束不是终点)

**现状问题**:`return Response` 即终点——响应回包后本次处理结束,下行推送无调用点
(Active gap "Wire MClientTargetResolver at session online/offline + CallClient sites")。

**闭环**:请求处理收束期(handler 返回前后),业务侧若有下行需求(欢迎/状态同步),
**经 Gateway 下行通道**推送——响应与下行为两条消息,先后经同一条客户端连接:

```
业务侧(LoginService / MPlayerService / 成员):
  1. 响应回包(原路返回:反射响应 → Gateway → 客户端)
  2. 下行:CallRemote(Gateway, "PushClientDownlink",
        { PlayerId, DownlinkFunctionId, Payload })          ← 服务间 RPC(非客户端协议)
Gateway:
  PushClientDownlink → MClientTargetResolver 按 PlayerId 查连接(持久注册表)
  → BuildClientEnvelopePacket(FunctionId, Payload) → 发下行
```

**下行目标解析(关键)**:跨进程下行的目标 = **PlayerId → conn 的持久注册表**
(`MClientTargetResolver.Connections`,`RegisterConn` 已按 `GetPlayerId()` 键控),
**不是**"当前绑定上下文"——异步响应路径下 `MClientTargetContextGuard` 早已析构。
- Guard/当前绑定:仅"同步处理期"便捷路径(请求→立即下行);
- 持久绑定表:异步/跨进程下行的依据(登录时 Gateway 绑定 connId↔playerId,§4.5)。

**登录闭环示例**:
```
客户端 → Login → LoginService 认证 → EnterGame → PlayerActor 创建/加载
  → ① 响应 LoginResponse(原路)
  → ② 业务侧 CallRemote(Gateway, "PushClientDownlink",
       { PlayerId, 欢迎FunctionId, 初始 View }) → Resolver 按 PlayerId 查连接 → 下行
```

## 7. 文件布局

```
Source/Servers/Login/
  LoginService.h/.cpp      零业务协议:进程骨架 + 持 MLoginAuth 成员(无容器)
  Members/
    MLoginAuth.h/.cpp      (Type=ActorMember)认证成员:Login/Logout(宿主 = 服务实例)
Source/Servers/Player/
  PlayerService.h/.cpp      零业务协议:进程骨架 + actor 集合
  MPlayerActor.h/.cpp       每玩家 actor(容器:状态机/数据生命周期/成员挂载)
  PlayerState.h             状态机枚举(Offline/LoggingIn/Online/Reconnecting)
  Members/
    MPlayerLogin.h/.cpp     (Type=ActorMember)登录/会话激活成员
    MPlayerStats.h/.cpp (Type=ActorMember)基础属性成员
    MPlayerItemContainer.h/.cpp (Type=ActorMember)背包成员
    MPlayerQuest.h/.cpp     (Type=ActorMember)任务成员
Source/Protocol/Messages/Player/
  FPlayerMessages.h         Login/Logout/在线业务消息(§4.2)
  FPlayerDataSchemas.h      Profile/Inventory/Quest Schema
```

## 8. 实现顺序

| 步骤 | 内容 | 依赖 |
|---|---|---|
| **F0(框架演进,先行)** | `EClassKind.Actor/ActorMember` + `IActorMember` + MHeaderTool 支持(`Type=ActorMember`、member 注册宏、`GetActorMember<T>()`)+ member 上 ServerCall 的 FunctionId 注册与寻址(请求显式带 PlayerId)——详见 `2026-08-14-actor-member-framework.md` | — |
| F1 | 框架验证成员:登录/背包作为 `ActorMember` 全链路(LoginService 认证 + PlayerService 持 actor + member 协议) | F0 |
| P1 | LoginService(认证/建号/选实例/踢旧)+ PlayerService 骨架(进程/配置/Registry)+ **跨进程登录链路**(LoginService→Gateway→EnterGame) | F1 + DBService M1 后 |
| P2 | 在线业务成员铺开(背包/任务/属性) | P1 |
| P3 | 状态机完整:断线/重连(宽限期 60s)/踢下线 | P2 |
| P4 | Global actor 接入(排行榜/邮件) | P2 |
