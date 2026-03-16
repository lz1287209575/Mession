# Mession TODO

> 只维护这一份 TODO。  
> 当前主链路目标：**Client -> Gateway -> Router -> OtherSvr**。  
> 当前阶段判断：主链路已经跑通，下一阶段重点不是扩功能，而是把接入方式、路由机制、验证基线彻底收口。

## Snapshot

当前已经明确并落地的事实：

- [x] Router / Gateway / Login / World / Scene 主链路已稳定联通。
- [x] Gateway 的客户端入口已经开始从手写分发转向 `MFUNCTION(Client, Message=...)` 驱动。
- [x] `MT_PlayerMove` 已接入首条声明式路由：
  `Route=RouterResolved, Target=World, Auth=Required, Wrap=PlayerClientSync`
- [x] `MT_Chat` 已接入第二条声明式路由：
  `Route=RouterResolved, Target=World, Auth=Required, Wrap=PlayerClientSync`
- [x] `MT_Heartbeat` 已接入首条声明式本地处理入口：
  无后端路由，直接由 Gateway 本地消费并进入 debug 观测。
- [x] `MT_Handshake` 已从旧的 Login 转发过渡路径收口为 Gateway 本地声明式入口。
- [x] Gateway 已具备 `RouterResolved` 所需的最小能力：
  pending/replay、in-flight 去重、最小 route cache、基础失效处理、debug 观测。
- [x] `HttpDebugServer` 已恢复可用，启动阶段会真实校验 bind/listen 成功。
- [x] Gateway / Router / Login / World / Scene 的 debug JSON 已修正为合法输出。
- [x] `Scripts/validate.py` 已覆盖：
  Handshake 本地处理、登录、复制、`RouterResolved` 路由缓存、Chat、Heartbeat 本地处理、RPC、断线清理、登录后立刻断开、双端同时断线、同一 `PlayerId` 快速重连、并发登录。

当前核心判断：

- 现在的主瓶颈不是 AOI。
- 现在更应该优先解决“声明式接入是否能成为默认路径”。
- Gateway 的长期职责应继续收敛到接入、鉴权、路由、转发、观测，而不是继续沉淀业务绑定逻辑。

## Architecture Guardrails

这些不是待办，而是后续改动必须遵守的边界：

- Gateway 负责 Client ingress、鉴权、路由查询、转发、连接态管理、观测。
- Router 负责服务发现、路由决策、目标服务选择，是控制面核心。
- OtherSvr 负责业务逻辑，不把业务判断重新回流到 Gateway。
- 客户端协议保持 message-based ingress；Gateway 内部再转成统一路由/反射运行时。

## P0

### P0-1. 把声明式客户端接入做成默认路径

目标：新增客户端消息时，默认不再需要手写 Gateway glue。

- [ ] 继续扩 `MFUNCTION(Client, Message=...)` 覆盖面，让更多客户端消息走生成入口。
- [ ] 把 `Route/Auth/Wrap/Target` 从首条样例扩成可复用模式，而不是只服务 `MT_PlayerMove`。
- [ ] 梳理 Gateway 里仍然保留的“按消息类型写死业务入口”，逐条迁到声明式路由路径。
- [ ] 明确哪些消息仍允许走 legacy 手写逻辑，哪些从现在开始必须走生成路径。

完成标准：

- 新增一个普通客户端消息时，不需要在 `GatewayServer` 中再补一套重复的包解析和分发胶水。
- Gateway 中客户端入口的新增代码主要表现为声明，而不是分支逻辑。

建议拆分顺序：

#### P0-1.a 第一批迁移对象

- [x] `MT_Handshake`
  结果：已收敛为 Gateway 本地声明式入口，不走 RouterResolved，也不再作为“转发到 Login 的伪登录消息”保留。
- [x] `MT_Chat`
  结果：已作为第二条声明式跨服样例接入，当前先落到 `RouterResolved -> World`，并已补脚本验证。
- [x] `MT_Heartbeat`
  结论：已收敛为 Gateway 本地处理入口，用于连接保活与接入层观测，不进入 RouterResolved。
- [ ] 盘点后续候选客户端消息
  目标：列出“适合优先迁移”和“暂时保留 legacy”的消息表，避免每次临时判断。

#### P0-1.b 每条消息的落地模板

- [ ] 补消息声明
  在服务头文件中补 `MFUNCTION(Client, Message=..., Route=..., Target=..., Auth=..., Wrap=...)`。
- [ ] 补 payload 绑定
  明确该消息使用已有 payload 还是新增 payload 结构，并走统一解析路径。
- [ ] 补 wrap 策略
  明确是 `PlayerClientSync`、`LoginRpcOrLegacy`，还是需要新增标准 wrap。
- [ ] 补 Gateway fallback 策略
  明确生成入口失败时是否允许回退 legacy；默认应逐步收紧，而不是一直双轨常驻。
- [ ] 补最小验证
  至少有一条脚本或断言覆盖“消息发出 -> 路由/本地处理 -> 可观察结果”。

#### P0-1.c 首轮建议执行顺序

- [x] 第一步：把 `MT_Handshake` 的定位定掉
  已落地：`MT_Handshake` 是连接建立阶段的边界消息，现已由 Gateway 本地声明式入口消费。
- [x] 第二步：选 `MT_Chat` 作为第二条声明式跨服样例
  已落地并通过验证，说明 `PlayerClientSync` 包装并不只适用于移动消息。
- [ ] 第三步：整理一份 Gateway 客户端消息矩阵
  按“本地处理 / Login 路由 / RouterResolved / 暂留 legacy”分类，作为后续迁移底表。
- [ ] 第四步：每迁一条消息就同步补 `validate.py` 或对应脚本
  保持“迁移一条，锁定一条”。

#### P0-1.d Gateway 客户端消息矩阵

当前建议分类如下：

- [x] `MT_Handshake` -> `GatewayLocal`
  已落地：这是连接层/协议层边界消息，现已不再耦合 Login。
- [x] `MT_Login` -> `Login`
  当前已接入声明式入口，`Wrap=LoginRpcOrLegacy`。
- [x] `MT_PlayerMove` -> `RouterResolved -> World`
  当前已作为首条样例落地，`Wrap=PlayerClientSync`。
- [x] `MT_Chat` -> `RouterResolved -> World`
  当前已作为第二条样例落地，`Wrap=PlayerClientSync`，用于验证非移动类消息的声明式转发。
- [x] `MT_Heartbeat` -> `GatewayLocal`
  已落地为 Gateway 本地消费，用于连接保活和状态观测。
- [ ] `MT_RPC` -> `Legacy / Deferred`
  当前已从“通用 fallback 转发”收紧为“唯一显式 legacy policy”，且已接 debug/脚本观测；后续再决定是继续声明式化还是单独协议化。
- [ ] `MT_ActorCreate / MT_ActorDestroy / MT_ActorUpdate / MT_LoginResponse / MT_Error` -> `S2C only`
  这些当前更像服务端下行消息，不应进入客户端上行接入迁移范围。

矩阵约束：

- [ ] 所有 `GatewayLocal` 消息都要明确“不转发”的理由，避免本地逻辑膨胀成业务黑洞。
- [ ] 所有 `RouterResolved` 消息都要有 route cache / 失效 / 回放的最小验证。
- [ ] 所有 `Login` 路由消息都要逐步收敛到统一 wrap，而不是继续散落特判。

### P0-2. 补齐生成链路可靠性

目标：让“用了宏就能稳定生成、稳定注册、稳定运行”成为默认预期。

- [ ] 补齐 `MFUNCTION` native 自动注册在带参数、`const`、返回值、复杂 payload 参数上的覆盖。
- [ ] 明确 unsupported 分支的边界，避免“未支持但能跑一点”长期混在主路径里。
- [ ] 收敛生成产物和构建日志噪音，把真正异常和正常提示分开。
- [ ] 继续整理 `MHeaderTool` 的归属规则和 override 机制，补插件、跨模块、特殊路径场景。

完成标准：

- 常见函数声明形式都能被自动注册。
- 生成失败或不支持场景能直接定位，而不是运行时碰运气。

### P0-3. 补齐枚举与 metadata 最小闭环

目标：先把最基本的“能生成、能注册、能查询”做完整。

- [ ] 补齐 `MENUM` 的完整生成与注册落地。
- [ ] 推进统一 metadata 机制，支持 `meta=(Key=Value, ...)` 落到类、属性、函数、枚举。
- [ ] 明确 metadata 在生成代码、运行时查询、后续复制系统中的落点，避免各处自定义一套。

完成标准：

- `MENUM` 不再只是被扫描到，而是能进入完整 reflection 数据。
- metadata 不再只停留在解析层，而能进入运行时可读结构。

### P0-4. 把验证基线跟着接入方式一起升级

目标：每推进一层自动化，就同步补一层自动验证。

- [ ] 每新增一类 `MFUNCTION(Client, Message=...)`，都补最小脚本验证。
- [ ] 为声明式路由补更多正向验证：
  登录后消息转发、route cache 命中、route cache 失效与重建。
- [ ] 继续增加 HTTP debug 可观测字段，让脚本尽量少依赖日志模糊匹配。
- [ ] 整理哪些验证属于“默认基线”，哪些属于“扩展压力/夜间回归”。

完成标准：

- 新接一条客户端声明式路由时，可以同步把它纳入脚本验证。
- 排查问题时优先看 HTTP debug / 明确状态字段，而不是先翻日志。

## P1

### P1-1. 路由与拓扑验证增强

- [ ] 补多区 / 多 World / 路由切换验证。
- [ ] 补 route invalidation / refresh 场景，覆盖服务切换、目标失活、重试与回放。
- [ ] 继续弱化 Gateway 对固定后端连接语义的依赖，向“Router 决策后的目标服务转发”收敛。

### P1-2. 断线与失败注入

- [ ] 在已有“立即断线 / 双端同时断线 / 快速重连”基础上补更极端时序。
- [ ] 增加登录中断、路由查询超时、后端断链、回放失败等失败注入。
- [ ] 统一断线、路由失败、协议失败的错误码和日志格式，方便脚本断言。

### P1-3. 压力回归常态化

- [ ] 把 `validate.py --stress` 做成稳定入口，先用于夜间或手动回归。
- [ ] 稳定后再决定是否纳入 CI 主路径。

## P2

### P2-1. 协议字节序继续推进

- [ ] 继续扩大网络序覆盖范围，优先收敛 `MessageUtils` / `PacketCodec` 边界。
- [ ] 为每批新增网络序消息补 `verify_protocol.py` 的 round-trip / fixed-blob 验证。
- [ ] 明确客户端协议与跨服协议的长期字节序策略，再继续铺开。

### P2-2. 反射类型体系补完

- [ ] 从通用 `MProperty` 继续拆到 `Struct / Enum / Object / Array / Map / Set`。
- [ ] 补对象引用、路径、默认值快照、CDO 等基础能力。
- [ ] 继续增强容器与嵌套类型反射，减少复杂类型特判。

### P2-3. 复制系统进一步元数据化

- [ ] 将复制系统逐步挂回反射元数据。
- [ ] 逐步支持 `MPROPERTY(Replicated)`、`RepNotify=Func`、条件复制。
- [ ] 明确复制规则和反射表的边界，避免复制逻辑长期散在运行时硬编码中。

## Deferred

### AOI

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
- [x] `Scripts/validate.py` Test 8: RPC 路径可达
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

## Next Step Recommendation

如果下一轮只做一件事，优先建议：

1. 先把 `MT_Handshake` 的职责边界定掉，明确它是否进入声明式接入体系。
2. 然后选 `MT_Chat` 做第二条声明式跨服样例。
3. 同步补脚本验证，并整理 Gateway 客户端消息矩阵。
