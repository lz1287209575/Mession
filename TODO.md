# TODO

这份文档记录当前这一轮重构之后，最值得继续推进的近期待办。

## 当前已稳定

- `Player` 已拆成 `Session / Controller / Pawn / Profile / Inventory / Progression / CombatProfile`
- `WorldClientServiceEndpoint -> WorldPlayerServiceEndpoint -> PlayerProxyCall -> Player 子对象` 这条普通 Player RPC 链路已经打通
- 客户端查询入口已覆盖：
  `QueryProfile / QueryPawn / QueryInventory / QueryProgression`
- 客户端写操作已覆盖：
  `ChangeGold / EquipItem / GrantExperience / ModifyHealth`
- 最小战斗链路 `Client_CastSkill` 已接通
- `Scripts/validate.py --build-dir Build --no-build` 当前覆盖：
  登录、切场、查询、写操作、双玩家场景同步、战斗、登出重登、错误链路

## 当前最优先

### 1. 收口玩家状态归属

当前最需要继续处理的是 `SceneId / Health / CombatResult` 的边界问题。

目标：

- `Profile` 只承担持久化画像
- `Pawn` 只承担场景运行时状态
- `Progression` 只承担成长数据
- `CombatProfile` 只承担战斗静态属性和战斗结算快照

不希望继续扩大跨对象桥接同步代码。

### 2. 固化验证与文档一致性

当前代码能力已经明显走在旧文档前面，后续需要把这个差距收回来。

目标：

- 保持 `README / Validation / Tooling / Roadmap` 与代码同步
- 新增主链路能力时同步补进 `validate.py`
- 把已稳定链路拆成更细粒度回归，而不是只堆在一个大脚本里

### 3. 继续下沉 World 入口胶水

`WorldPlayerServiceEndpoint` 已经比之前干净，但还可以继续压缩。

目标：

- 重复适配代码继续收口
- 普通 Player 业务更多直接落到对象树
- World 层只保留真正需要跨服编排的 workflow

## 第二优先级

### 4. 强化对象域驱动

- 继续减少 fallback 序列化和临时兼容逻辑
- 扩大 `PersistentData / Replicated` 的真实字段覆盖范围
- 统一对象树回放、持久化和复制的行为边界

### 5. 推进数据驱动战斗

- 减少对 `SkillCatalog::LoadBuiltInDefaults()` 的依赖
- 补强 `uasset -> compiled spec -> server runtime` 的真实使用链路
- 给战斗链路补更明确的验证和错误诊断

### 6. 建设控制面和观测能力

- 统一健康信息接口
- 各服务接入结构化 debug status
- 补任务输出和多机控制的稳定性

## 暂不优先

这些方向值得做，但不该排在当前主线前面：

- 继续增加零散 Player RPC
- 过早扩展更多玩法表面能力
- 为了抽象而抽象的 Actor / Component 重命名整理

当前更重要的是先把状态边界、验证和工具链收紧。
