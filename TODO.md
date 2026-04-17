# TODO

这份文档记录当前这一轮重构之后，最值得继续推进的近期待办。

更新时间基线：

- 当前内容已对齐到 `2026-04-15` 这次运行时与验证分层推进之后的仓库状态
- 已确认 `cmake --build Build -j4` 可通过
- 已确认 `Scripts/validate.py --build-dir Build --no-build` 全量验证通过
- 已确认 `Scripts/validate.py` 支持 suite 分组：
  `all / player_state / scene_downlink / combat_commit / forward_errors / runtime_dispatch`

## 当前已稳定

- `Player` 已拆成 `Session / Controller / Pawn / Profile / Inventory / Progression / CombatProfile`
- `WorldClient -> WorldClientCommon::FRequest::Dispatch -> PlayerService -> Player 子对象 / DoXxx(...)` 这条普通 Player RPC 链路已经打通
- 客户端查询入口已覆盖：
  `QueryProfile / QueryPawn / QueryInventory / QueryProgression / QueryCombatProfile`
- 客户端写操作已覆盖：
  `ChangeGold / EquipItem / GrantExperience / ModifyHealth / SetPrimarySkill`
- 最小战斗链路 `Client_CastSkill` 已接通
- `Scripts/validate.py --build-dir Build --no-build` 当前覆盖：
  登录、切场、查询、写操作、双玩家场景同步、战斗、登出重登、错误链路
- `World` 当前主链路已经切到 `Runtime Dispatch + Fiber Await` 风格，显式 `Workflow` 主链路已基本收掉
- `WorldClient -> PlayerService -> ObjectCall` 这条调用边界已开始收口：
  `StartAsyncClientResponse / ResultFutureSupport / ObjectCall helper / ObjectCallRouter resolve` 已基本对齐

## 当前最优先

### 1. 收口运行时基础层

当前最需要继续处理的不是再抽局部业务胶水，而是把 `Runtime Dispatch + Fiber Await` 这条地基收紧。

当前优先关注：

- `FiberAwait / FiberScheduler / ResultFutureSupport / ServerCallAsyncSupport / PlayerCommandRuntime`
- `PlayerCommandRuntime` 当前同时承担：
  玩家串行化、多人命令 barrier、fiber 生命周期包装
- `FiberScheduler` 当前仍存在平台能力差异
  其中 Windows backend 还是占位实现，不应误判为完整跨平台语义
  当前已经补了显式 fail-fast：一旦走到 suspend/resume，会明确返回 `fiber_backend_unsupported`，不再只冒出含糊的 null backend 异常

目标：

- 明确“同步等待 / 运行时排队 / detached fiber / result future 包装”各自边界
- 不让 Runtime 特殊逻辑继续散落回业务函数
- 给运行时层补更明确的专项验证，而不是只依赖黑盒全量脚本

### 2. 固化验证与文档一致性

当前不是“完全缺文档”，而是文档层次已经增多，需要把旧入口文档和新专题文档重新对齐。

目标：

- 保持 `README / Validation / Tooling / Roadmap` 与当前代码同步
- 统一 `README / TODO / Roadmap / RepositoryArchitecture` 之间的优先级表述，避免互相打架
- 新增主链路能力时同步补进 `validate.py`
- 把已稳定链路拆成更细粒度回归，而不是只堆在一个大脚本里
- 当前已先补 suite 入口：
  `player_state / runtime_social / scene_downlink / combat_commit / forward_errors / runtime_dispatch`
- 后续继续把 suite 从“分组入口”推进到“更明确的专项回归”
  当前 `runtime_social` 已开始覆盖多参与者 `PlayerCommandRuntime` 链路

### 3. 收口玩家状态归属

当前仍需要继续处理 `SceneId / Health / CombatResult` 的边界问题，但它已经不是唯一主线，更多是防回弹和补约束。

目标：

- `Profile` 只承担持久化画像
- `Pawn` 只承担场景运行时状态
- `Progression` 只承担成长数据
- `CombatProfile` 只承担战斗静态属性和战斗结算快照

当前需要重点收掉的重复状态来源：

- `SceneId` 仍分散在 `Controller / Pawn / Profile`
  但当前已确认登录恢复主权可以更多落到 `Profile`，`Controller.SceneId` 不再需要持久化桥
  并且 route 更新不再直接回写 `Profile.CurrentSceneId`，持久化场景只在成功 enter/switch/logout 收口时同步
- `Health` 仍分散在 `Pawn / Progression / CombatProfile`
  当前已进一步收口到：`Progression.Health` 作为主状态，`Pawn.Health` 只在 spawned 时作为运行时镜像，`CombatProfile.LastResolvedHealth` 作为战斗结算快照
  `ResolveCurrentHealth()` 已不再把未 spawned 的 `Pawn.Health` 当作主状态 fallback，`SyncRuntimeState()` 也不再重复回写血量
- 战斗结算结果已经进入链路，且 `ExperienceReward` 已开始落到 `Progression`
  当前已确认 `LastResolvedSceneId / LastResolvedHealth` 没有业务读取方，应继续保持为快照、诊断和回放字段，不再参与主状态决策

不希望继续扩大跨对象桥接同步代码。

### 4. 继续下沉 World 入口胶水

`World` 入口已经比之前干净，但还可以继续压缩。

目标：

- 重复适配代码继续收口
- 普通 Player 业务更多直接落到对象树
- World 层只保留真正需要跨服编排和依赖协调的入口
- 不要让业务主链路重新长回显式 `Workflow / OnStep / callback graph`
- 继续防止同一条调用链在 `WorldClient / PlayerService / ObjectCall` 三层里重复写 transport glue

## 第二优先级

### 5. 强化对象域驱动

- 继续减少 fallback 序列化和临时兼容逻辑
- 扩大 `PersistentData / Replicated` 的真实字段覆盖范围
- 统一对象树回放、持久化和复制的行为边界

### 6. 推进数据驱动战斗

- 减少对 `SkillCatalog::LoadBuiltInDefaults()` 的依赖
- 补强 `uasset -> compiled spec -> server runtime` 的真实使用链路
- 给战斗链路补更明确的验证和错误诊断
- 把 `CommitCombatResult` 一类链路正式纳入主验证，而不是只停留在结构已具备

### 7. 建设控制面和观测能力

- 统一健康信息接口
- 各服务接入结构化 debug status
- 补任务输出和多机控制的稳定性

## 暂不优先

这些方向值得做，但不该排在当前主线前面：

- 继续增加零散 Player RPC
- 过早扩展更多玩法表面能力
- 为了抽象而抽象的 Actor / Component 重命名整理

当前更重要的是先把状态边界、验证、文档一致性和运行时收口。
