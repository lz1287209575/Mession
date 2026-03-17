# Mession TODO

> 只维护这一份 TODO。  
> 当前主链路目标：**Client -> Gateway -> Router -> OtherSvr**。  
> 当前阶段判断：主链路已跑通，下一阶段重点是把客户端统一函数调用、路由收口、验证基线做成默认工作方式。

## Snapshot

当前已经明确并落地的事实：

- [x] Router / Gateway / Login / World / Scene 主链路已稳定联通。
- [x] Gateway 客户端入口已开始从手写分发转向 `MFUNCTION(Client, Message=...)` 驱动。
- [x] `MT_Handshake` 已收口为 Gateway 本地声明式入口。
- [x] `MT_Login` 已接入声明式入口，当前使用 `Wrap=LoginRpcOrLegacy`。
- [x] `MT_PlayerMove` 已接入首条声明式跨服路由：
  `Route=RouterResolved, Target=World, Auth=Required, Wrap=PlayerClientSync`
- [x] `MT_Chat` 已接入第二条声明式跨服路由：
  `Route=RouterResolved, Target=World, Auth=Required, Wrap=PlayerClientSync`
- [x] `MT_Heartbeat` 已收口为 Gateway 本地声明式入口，用于连接保活与观测。
- [x] Gateway 已具备 `RouterResolved` 所需最小能力：
  pending/replay、in-flight 去重、最小 route cache、基础失效处理、debug 观测。
- [x] `HttpDebugServer` 已恢复可用，启动阶段会真实校验 bind/listen 成功。
- [x] Gateway / Router / Login / World / Scene 的 debug JSON 已修正为合法输出。
- [x] 复制链路的基础可观察性已补强：
  actor 初始同步、连接关闭回调、发送失败日志、服务器日志落盘。
- [x] `Scripts/validate.py` 已覆盖：
  Handshake、本地登录主链路、复制、`RouterResolved` 路由缓存、Chat、Heartbeat、客户端 `MT_RPC` 兼容路径、断线清理、登录后立刻断开、双端同时断线、同一 `PlayerId` 快速重连、并发登录。
- [x] 统一 `FunctionID` 规则已经整理成文档，见 [function-id-rules.md](/workspaces/Mession/Docs/function-id-rules.md)。
- [x] 客户端统一函数调用已经确定为长期方向，设计草案见 [client-unified-function-call.md](/workspaces/Mession/Docs/client-unified-function-call.md)。

当前核心判断：

- 现在的主瓶颈不是 AOI。
- 现在更应该优先解决“客户端统一函数调用能否成为默认路径”。
- Gateway 的长期职责应继续收敛到接入、鉴权、路由、转发、连接态管理、观测，而不是继续沉淀业务绑定逻辑。
- 当前最缺的不是样例数量，而是统一函数入口、生成边界、验证约束这三类“规则”。

## Architecture Guardrails

这些不是待办，而是后续改动必须遵守的边界：

- Gateway 负责 Client ingress、鉴权、路由查询、转发、连接态管理、观测。
- Router 负责服务发现、路由决策、目标服务选择，是控制面核心。
- OtherSvr 负责业务逻辑，不把业务判断重新回流到 Gateway。
- 客户端长期目标是统一函数调用 ingress；迁移期允许保留兼容包头，但不再继续扩展按业务消息号分裂的入口模型。
- `Server <-> Server` 的 `MT_RPC` 保留为内部 RPC 通道；`Client -> Gateway` 的 `MT_RPC` 不作为长期正式协议，只保留为受控兼容路径并以移除为目标。
- 下行消息和上行消息分开管理；`S2C only` 消息不进入客户端上行迁移讨论。

## Now

### N1. 整理 Gateway 客户端消息矩阵

目标：把“哪些消息应该怎么接入 Gateway”从隐含知识变成显式规则。

- [x] 在 TODO 中固化完整矩阵，按 `GatewayLocal / Login / RouterResolved / Legacy / S2C only` 分类。
- [x] 为每条消息写明原因，而不只是写目标归类。
- [x] 明确哪些消息从现在开始必须走声明式入口，哪些允许暂留 legacy。
- [x] 明确 `MT_RPC` 的兼容边界、替代路径和最终移除条件。

完成标准：

- 新增一个客户端消息时，先能在矩阵里找到归类与接入方式，而不是临时判断。
- Gateway 中的 fallback 不再增长成“默认兜底路径”。

建议先把现有消息收口为以下矩阵：

- [x] `MT_Handshake` -> `GatewayLocal`
  原因：连接建立阶段的边界消息，只更新 Gateway 连接态和 debug 观测，不应耦合到 Login 或 World。
  当前状态：已声明式接入 `Client_Handshake`，本地消费。
  验证：`validate.py` Test 1。
- [x] `MT_Login` -> `Login`
  原因：认证与会话建立属于 Login 职责，不应由 Gateway 内嵌业务判断。
  当前状态：已声明式接入，`Wrap=LoginRpcOrLegacy`，允许 RPC 优先、typed legacy 保底。
  验证：`validate.py` Test 2，以及后续登录相关基线。
- [x] `MT_PlayerMove` -> `RouterResolved -> World`
  原因：玩家运动是典型业务消息，需要带玩家身份进入 World，不应停留在 Gateway 本地。
  当前状态：已声明式接入，`Auth=Required`，`Wrap=PlayerClientSync`。
  验证：`validate.py` Test 3、4、5。
- [x] `MT_Chat` -> `RouterResolved -> World`
  原因：这是第二条非移动类业务消息样例，用来证明 `PlayerClientSync` 不只服务移动同步。
  当前状态：已声明式接入，`Auth=Required`，`Wrap=PlayerClientSync`。
  验证：`validate.py` Test 6。
- [x] `MT_Heartbeat` -> `GatewayLocal`
  原因：它属于接入层保活与观测消息，不需要 Router 决策，也不应该下沉成业务黑洞。
  当前状态：已声明式接入 `Client_Heartbeat`，本地消费并更新 debug 字段。
  验证：`validate.py` Test 7。
- [x] `MT_RPC` -> `Legacy / Deferred`
  原因：客户端 `MT_RPC` 不是长期正式协议，只作为受控兼容路径保留，避免 Gateway 再次变成“万能反射入口”。
  当前状态：当前是唯一显式 legacy fallback，仅在生成入口未命中时且消息类型为 `MT_RPC` 才允许进入。
  验证：`validate.py` Test 8。
  约束：不再新增新的客户端 `MT_RPC` 用法；后续新增能力优先拆成明确消息类型。
  移除条件：现有客户端侧 `MT_RPC` 用途被明确消息完全替代，并已有脚本覆盖。
- [x] `MT_LoginResponse / MT_ActorCreate / MT_ActorDestroy / MT_ActorUpdate / MT_Error` -> `S2C only`
  原因：这些消息当前语义都是服务端下行；不应进入客户端上行接入迁移讨论，也不应误判为“待迁移上行消息”。
  当前状态：仍作为客户端接收路径存在，其中复制相关消息已被 Test 3 间接覆盖。

矩阵规则：

- [x] `GatewayLocal` 只允许连接边界、保活、纯观测类消息；不承接跨服业务判断。
- [x] `Login` 类消息默认必须声明式接入；Gateway 只负责转发与会话绑定。
- [x] `RouterResolved` 类消息默认必须具备 `Auth=Required`、route cache、失效与 replay 的最小验证。
- [x] `Legacy` 只允许显式白名单，不允许“生成失败就自动退回 legacy”。
- [x] `S2C only` 不进入客户端上行迁移范围，避免 TODO 混淆上下行职责。

从现在开始的接入约束：

- [x] 新增普通客户端上行消息时，默认先在这张矩阵里归类，再决定代码接入方式。
- [x] 除 `MT_RPC` 外，不再新增新的客户端 legacy fallback。
- [x] 如果一条新消息无法放进 `GatewayLocal / Login / RouterResolved` 三类之一，先回到架构边界重新审视，而不是直接写分支。
- [ ] 下一步基于这张矩阵，挑一条仍然依赖手写 glue 的消息或流程，作为 `N2` 的下一条迁移对象。

### N2. 把声明式客户端接入做成默认路径

目标：新增客户端能力时，默认不再需要手写 Gateway glue，也不再需要新增业务消息号。

- [ ] 固化客户端统一函数调用的 wire format，设计草案见 [client-unified-function-call.md](/workspaces/Mession/Docs/client-unified-function-call.md)。
- [ ] 在 Gateway 中引入 `MT_FunctionCall` 入口，按 `FunctionID + PayloadSize + Payload` 分发到生成的 client-call manifest。
- [ ] 生成 `FunctionID -> decode -> invoke` glue，而不是继续扩展 `MessageType -> Function`。
- [ ] 把 `Route/Auth/Wrap/Target` 从当前样例扩成可复用模式，而不是只服务 `PlayerMove/Chat`。
- [ ] 明确统一函数入口失败时的 fallback 策略；默认应逐步收紧，而不是长期双轨常驻。
- [ ] 为每类 wrap 形成清晰模板，例如 `GatewayLocal`、`LoginRpcOrLegacy`、`PlayerClientSync`。
- [ ] 挑选 `Handshake / Login / Chat` 作为首批统一函数调用垂直切片。

完成标准：

- 新增一个普通客户端能力时，不需要在 `GatewayServer` 中补重复的包解析和分发胶水。
- Gateway 中客户端入口的新增代码主要表现为声明，而不是分支逻辑或枚举扩展。

### N3. 把验证基线跟着接入方式一起升级

目标：每推进一层自动化，就同步补一层自动验证。

- [ ] 每新增一类 `MFUNCTION(Client, Message=...)`，都补最小脚本验证。
- [ ] 为声明式路由补更多正向验证：登录后消息转发、route cache 命中、route cache 失效与重建。
- [ ] 继续增加 HTTP debug 可观测字段，让脚本尽量少依赖日志模糊匹配。
- [ ] 区分“默认基线”和“扩展压力/夜间回归”，避免所有验证都堆进一条脚本路径。

完成标准：

- 新接一条客户端声明式路由时，可以同步把它纳入脚本验证。
- 排查问题时优先看 HTTP debug / 明确状态字段，而不是先翻日志。

## Next

### N4. 补齐生成链路可靠性

目标：让“用了宏就能稳定生成、稳定注册、稳定运行”成为默认预期。

- [ ] 补齐 `MFUNCTION` native 自动注册在带参数、`const`、返回值、复杂 payload 参数上的覆盖。
- [ ] 明确 unsupported 分支的边界，避免“未支持但能跑一点”长期混在主路径里。
- [ ] 收敛生成产物和构建日志噪音，把真正异常和正常提示分开。
- [ ] 继续整理 `MHeaderTool` 的归属规则和 override 机制，补插件、跨模块、特殊路径场景。

完成标准：

- 常见函数声明形式都能被自动注册。
- 生成失败或不支持场景能直接定位，而不是运行时碰运气。

### N5. 补齐枚举与 metadata 最小闭环

目标：先把最基本的“能生成、能注册、能查询”做完整。

- [ ] 补齐 `MENUM` 的完整生成与注册落地。
- [ ] 推进统一 metadata 机制，支持 `meta=(Key=Value, ...)` 落到类、属性、函数、枚举。
- [ ] 明确 metadata 在生成代码、运行时查询、后续复制系统中的落点，避免各处自定义一套。

完成标准：

- `MENUM` 不再只是被扫描到，而是能进入完整 reflection 数据。
- metadata 不再只停留在解析层，而能进入运行时可读结构。

### N6. 路由与失败注入增强

- [ ] 补多区 / 多 World / 路由切换验证。
- [ ] 补 route invalidation / refresh 场景，覆盖服务切换、目标失活、重试与回放。
- [ ] 在已有“立即断线 / 双端同时断线 / 快速重连”基础上补更极端时序。
- [ ] 增加登录中断、路由查询超时、后端断链、回放失败等失败注入。
- [ ] 统一断线、路由失败、协议失败的错误码和日志格式，方便脚本断言。

## Later

### L1. 压力回归常态化

- [ ] 把 `validate.py --stress` 做成稳定入口，先用于夜间或手动回归。
- [ ] 稳定后再决定是否纳入 CI 主路径。

### L2. 协议字节序继续推进

- [ ] 继续扩大网络序覆盖范围，优先收敛 `MessageUtils` / `PacketCodec` 边界。
- [ ] 为每批新增网络序消息补 `verify_protocol.py` 的 round-trip / fixed-blob 验证。
- [ ] 明确客户端协议与跨服协议的长期字节序策略，再继续铺开。

### L3. 反射类型体系补完

- [ ] 从通用 `MProperty` 继续拆到 `Struct / Enum / Object / Array / Map / Set`。
- [ ] 补对象引用、路径、默认值快照、CDO 等基础能力。
- [ ] 继续增强容器与嵌套类型反射，减少复杂类型特判。

### L4. 复制系统进一步元数据化

- [ ] 将复制系统逐步挂回反射元数据。
- [ ] 逐步支持 `MPROPERTY(Replicated)`、`RepNotify=Func`、条件复制。
- [ ] 明确复制规则和反射表的边界，避免复制逻辑长期散在运行时硬编码中。

## Deferred

### D1. AOI

- [ ] 重做 `AOIComponent` 数据结构语义。
- [ ] 明确格子划分、跨格移动、进入视野、离开视野、离线清理模型。
- [ ] 再把 AOI 结果接入 `WorldServer` 可见性计算和 `ReplicationDriver::RelevantActors`。
- [ ] 补 AOI 对应验证。

延期原因：

- AOI 还不是当前主链路的关键瓶颈。
- 反射接入、路由收口、验证基线对当前迭代效率影响更大。

## Validation Baseline

当前默认基线：

- [x] `Scripts/verify_protocol.py`
- [x] `Scripts/validate.py` Test 1: Handshake 本地处理
- [x] `Scripts/validate.py` Test 2: 登录主链路
- [x] `Scripts/validate.py` Test 3: 复制链路
- [x] `Scripts/validate.py` Test 4: `RouterResolved` 路由缓存建立
- [x] `Scripts/validate.py` Test 5: 断线清理与重连恢复
- [x] `Scripts/validate.py` Test 6: Chat 路径可达
- [x] `Scripts/validate.py` Test 7: Heartbeat 本地处理可达
- [x] `Scripts/validate.py` Test 8: 客户端 `MT_RPC` 兼容路径可达
- [x] `Scripts/validate.py` Test 9: 登录后立刻断开
- [x] `Scripts/validate.py` Test 10: 双端同时断线
- [x] `Scripts/validate.py` Test 11: 同一 `PlayerId` 快速重连
- [x] `Scripts/validate.py` Test 12: 并发登录

后续约束：

- [ ] 反射/生成链路改动必须配套最小验证。
- [ ] 协议改动必须同步更新 `verify_protocol.py`。
- [ ] 主链路行为改动必须同步更新 `validate.py` 或补明确脚本入口。

## Done Milestones

- [x] Socket / EventLoop / `MNetServerBase` 第一阶段收口。
- [x] 主链路登录、进世界、复制、登出状态回收已形成闭环。
- [x] `ParsePayload` / `BuildPayload` 已替换主要裸 `memcpy` 路径。
- [x] 首批消息头字段已切到网络字节序，并建立协议验证脚本。
- [x] `MHeaderTool` 已生成 reflection/client manifest。
- [x] 客户端协议方向已确定为 message-based ingress + Gateway 内部路由转发。
- [x] `HttpDebugServer` 已恢复可用，并重新接回 HTTP 状态探测。
- [x] 复制链路的基础观测已补强，包括 actor 初始同步、连接关闭回调、发送失败日志、服务器日志落盘。

## Recommendation

如果下一轮只做一件事，优先建议：

1. 先完成统一函数调用的兼容 wire format 设计。
2. 然后在 Gateway 中接入 `MT_FunctionCall` 分发骨架。
3. 再用 `Handshake / Login / Chat` 做第一批垂直切片，并同步补最小验证。
