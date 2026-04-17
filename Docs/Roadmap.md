# 后续路线

这份文档只记录当前代码基础上仍值得继续推进的方向，不再按历史里程碑记迁移过程。

## 1. 收口玩家状态归属

当前玩家对象树已经形成，但 `SceneId`、`Health`、战斗结算状态仍分散在：

- `Controller`
- `Pawn`
- `Profile`
- `Progression`
- `CombatProfile`

后续重点应该是：

- 明确持久化状态和运行时状态的唯一归属
- 减少 `SyncRuntimeState`、fallback 解析和跨对象桥接
- 让登录、切场、登出、战斗结算都走更清晰的状态边界

当前已经收口到的阶段性结论：

- `SceneId` 的恢复主权可以更多落到 `Profile.CurrentSceneId`
- `Controller.SceneId` 保留运行时 route 语义，不再需要持久化桥
- `Progression.Health` 是长期血量主状态
- `Pawn.Health` 是运行时镜像
- `CombatProfile.LastResolvedSceneId / LastResolvedHealth` 是战斗结算快照，不参与主状态决策

## 2. 完善验证与可观测性

当前主链路验证已经能覆盖查询、写操作、场景同步、最小战斗链路，但还不够系统。

后续优先补：

- 更细粒度的自动化回归
- Persistence / Replication 专项测试
- 并发 / RPC 运行时测试
- 更明确的错误码、统计和 debug 输出
- 结构化健康状态与诊断接口

## 3. 收敛异步流程表达

`MFuture / MPromise / Fiber Await / Runtime Dispatch` 已经形成基础分层，但业务编排风格仍需要继续统一。

方向包括：

- 继续减少散落的链式 lambda
- 让复杂服间流程继续留在统一 Runtime 驱动里，而不是重新长回显式 `Workflow`
- 明确“同步等待”“异步串联”“跨线程调度”的推荐边界

## 4. 继续压缩 `Server` 和 Endpoint 胶水

当前 `Server -> Rpc -> Endpoint -> Domain Object` 的分层已经成型，但仍有下沉空间。

继续推进的方向：

- 把重复的请求适配和转发逻辑收口到更稳定的 helper 或 route list
- 让普通 Player RPC 更自然地落到对象树
- 保持 `Server` 只承担进程边界和生命周期职责

## 5. 强化对象域驱动

当前 Persistence / Replication 已经共用对象域快照能力，后续应继续推进：

- 让更多真实玩法字段摆脱 fallback 逻辑
- 扩大 `MPROPERTY` 域标记覆盖范围
- 统一对象树重建、快照回放和版本推进路径

## 6. 推进数据驱动战斗和技能图

仓库已经有：

- `SceneCombatRuntime`
- `SkillCatalog`
- `UAssetSkillLoader`
- UE 侧技能图文档和共享节点注册表

下一阶段更值得做的是：

- 减少对内建默认技能的依赖
- 强化 `uasset -> compiled spec -> server runtime` 的真实链路
- 补战斗相关回归，而不是只把技能系统停留在结构层面

## 7. 继续建设控制面

当前仓库已经有：

- `server_control_api.py`
- `server_registry.py`
- `server_manager_tui.py`

后续如果继续做多机控制，建议优先补：

- 统一健康信息接口
- 结构化 debug status JSON
- 更稳定的任务输出和历史保留
- 更细粒度的多机批量控制与拓扑检查
