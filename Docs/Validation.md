# 验证策略

## 当前推荐验证层次

### 第 1 层：先过编译

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
cmake --build Build -j4
```

### 第 2 层：再过主链路回归

```bash
python3 Scripts/validate.py --build-dir Build --no-build
```

### 第 2.5 层：按专项 suite 过子链路

```bash
python3 Scripts/validate.py --build-dir Build --no-build --list-suites
python3 Scripts/validate.py --build-dir Build --no-build --suite player_state
python3 Scripts/validate.py --build-dir Build --no-build --suite scene_downlink
python3 Scripts/validate.py --build-dir Build --no-build --suite combat_commit
python3 Scripts/validate.py --build-dir Build --no-build --suite forward_errors
python3 Scripts/validate.py --build-dir Build --no-build --suite runtime_dispatch
```

### 第 3 层：需要长期观察时手工起服

```bash
python3 Scripts/servers.py start --build-dir Build
```

## `validate.py` 当前覆盖内容

脚本会按顺序启动：

1. `RouterServer`
2. `MgoServer`
3. `LoginServer`
4. `WorldServer`
5. `SceneServer`
6. `GatewayServer`

然后验证这些链路：

- `Client_Login`
- `Client_FindPlayer`
- `Client_SwitchScene`
- `Client_Move`
- `Client_QueryPawn`
- `Client_ChangeGold`
- `Client_EquipItem`
- `Client_GrantExperience`
- `Client_ModifyHealth`
- `Client_QueryProfile`
- `Client_QueryInventory`
- `Client_QueryProgression`
- 双玩家同场景下的 `Client_ScenePlayerEnter / Update / Leave`
- `Client_CastSkill`
- 登出后重登与状态恢复
- forwarded `ClientCall` 的参数绑定失败
- forwarded `ClientCall` 的业务校验失败
- World 不可用时 Gateway 的错误返回

## `validate.py` 当前 suite 分组

- `all`
  默认完整回归，覆盖当前主链路
- `player_state`
  聚焦登录、查询、写操作、登出重登恢复
- `scene_downlink`
  聚焦双玩家同场景 enter / update / leave 下行
- `combat_commit`
  聚焦双玩家入场后的最小战斗提交链路，并检查战斗结果是否同步回 `Profile / Progression`
- `forward_errors`
  聚焦 Gateway forward 后的参数绑定、业务校验、后端不可用错误链路
- `runtime_dispatch`
  聚焦当前 World 主运行时链路覆盖集合，用来观察 Runtime Dispatch 主干回归

当前这些 suite 还不是完全解耦的单元级测试，而是从总回归脚本里切出的专项入口。
它们的作用是让我们先有“可快速定位的分层回归”，再继续往更细粒度专项测试演进。

## 为什么当前以这条链路为主

因为它同时穿过了仓库当前最关键的收敛点：

- Gateway 客户端入口
- `MT_FunctionCall`
- Login 会话签发与校验
- World 玩家对象树
- Scene 路由和场景同步
- Router 路由注册与查询
- Mgo 持久化边界
- 玩家状态写入后再查询和重登恢复
- 最小战斗执行链路

如果这条链路稳定，说明当前主骨架不仅能启动，而且对“读写状态、跨服协作、错误返回、最小玩法闭环”是可工作的。

## 建议的改动后验证

### 改 `Protocol`

- 跑编译
- 跑 `Scripts/verify_protocol.py`
- 跑 `validate.py`

### 改 `Gateway / World ClientCall`

- 跑编译
- 跑 `validate.py`
- 必要时用 `Scripts/test_client.py` 做定向调用

### 改 `Player` 对象树或状态归属

- 跑编译
- 跑 `validate.py`
- 重点检查：
  登录后初始状态
  写操作后查询结果
  登出后重登恢复结果

### 改 `Persistence / Replication`

- 跑编译
- 跑 `validate.py`
- 配合 `Scripts/debug_replication.py` 或服务日志
- 重点检查 dirty 清理、版本推进、对象快照是否合理

### 改 `Scene / Combat`

- 跑编译
- 跑 `validate.py`
- 重点看：
  双玩家同场景同步
  `Client_CastSkill`
  战斗结果回写后目标玩家状态

## 当前验证空白

虽然 `validate.py` 已经不再只是“最小登录链路”，但当前仓库仍缺少这些固定回归：

- 单元测试覆盖不足
- 并发运行时缺少专门测试
- Persistence / Replication 缺少更细粒度专项回归
- 多玩家、多服故障、多节点拓扑场景还未形成固定自动化回归集
- 控制面脚本缺少更系统的接口级测试

当前已经新增一层更明确的运行时回归入口：

- `runtime_social`
  覆盖 `CreateParty / InviteParty / AcceptPartyInvite / KickPartyMember`
  用来回归多参与者 `PlayerCommandRuntime` barrier 与下行通知链路
- `runtime_dispatch`
  当前也已额外检查战斗后目标玩家的 `QueryProfile / QueryProgression`，用于回归 Health 主状态是否仍落在 `Progression`

## 当前建议

后续如果继续迭代当前仓库，验证层面的优先级建议是：

1. 保持 `validate.py` 与主链路能力同步
2. 先把当前 suite 入口维持可用，再逐步拆成更细粒度用例
3. 优先补运行时、对象域、复制、持久化专项测试
4. 再去扩展更多玩法或多机控制场景
