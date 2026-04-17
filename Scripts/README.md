# Scripts 目录说明

`Scripts/` 放的是开发、验证和控制面辅助脚本，不承载正式服务器逻辑。

## 主要脚本

### `servers.py`

开发期一键起停服：

```bash
python3 Scripts/servers.py start --build-dir Build
python3 Scripts/servers.py stop --build-dir Build
```

默认启动：

- `RouterServer`
- `LoginServer`
- `WorldServer`
- `SceneServer`
- `GatewayServer`

适合本地调试、长期看日志、手工连客户端。

### `validate.py`

当前仓库最重要的自动回归脚本：

```bash
python3 Scripts/validate.py --build-dir Build
python3 Scripts/validate.py --build-dir Build --no-build
python3 Scripts/validate.py --build-dir Build --no-build --suite player_state
python3 Scripts/validate.py --build-dir Build --no-build --suite scene_downlink
```

它会启动：

- `RouterServer`
- `MgoServer`
- `LoginServer`
- `WorldServer`
- `SceneServer`
- `GatewayServer`

并验证：

- 登录、查找玩家、切场景、移动
- `QueryPawn / QueryProfile / QueryInventory / QueryProgression`
- `ChangeGold / EquipItem / GrantExperience / ModifyHealth`
- 双玩家场景同步下行
- `Client_CastSkill`
- 登出后重登恢复
- forwarded `ClientCall` 异常链路

脚本内部按 `Client API` 稳定名计算函数 ID，不依赖 owner class 名。

当前支持的 suite：

- `all`：完整回归
- `player_state`：登录、查询、写操作、登出重登恢复
- `scene_downlink`：场景 enter/update/leave 下行链路
- `combat_commit`：双玩家入场后最小战斗提交链路
- `forward_errors`：转发参数绑定、业务校验、后端不可用错误链路
- `runtime_dispatch`：当前主运行时链路覆盖集合，包含登录到重登恢复的主要 World 流程

### `compile_assets.py`

统一编译 `*.mobj.json -> *.mob` 的资产工具：

```bash
python3 Scripts/compile_assets.py --build GameData/Combat/Monsters/Slime.mobj.json
python3 Scripts/compile_assets.py GameData/Combat/Monsters
python3 Scripts/compile_assets.py --no-roundtrip
python3 Scripts/compile_assets.py --publish GameData/Combat/Monsters/Slime.mobj.json
```

默认行为：

- 输入源：`*.mobj.json`
- 生成目录：`Build/Generated/Assets/...`
- 输出产物：`Build/Generated/Assets/.../*.mob`
- 验证输出：`Build/Generated/Assets/.../*.roundtrip.json`
- 可选发布：`--publish` 后再把 `.mob` 同步到 `GameData/...`
- 每次编译前会先清掉该资产旧的生成物，避免残留脏文件

命名规则固定为：

- `GameData/Combat/Monsters/Slime.mobj.json`
  -> `Build/Generated/Assets/Combat/Monsters/Slime.mob`
- `GameData/Combat/Monsters/Slime.mobj.json`
  -> `Build/Generated/Assets/Combat/Monsters/Slime.roundtrip.json`
- `--publish`
  -> `GameData/Combat/Monsters/Slime.mob`

内部调用 `MObjectAssetSmokeTool`，所以除了编译 `.mob`，也会顺手验证：

- `JSON -> .mob`
- `.mob -> Load`
- `Load -> Export JSON`
- 如果是 `MMonsterConfig`，额外验证一次 `MonsterManager` 生成 Monster

### `verify_protocol.py`

协议和函数 ID 相关检查脚本，适合在改消息结构或 RPC 元数据后运行。

### `test_client.py`

轻量测试客户端，用于直连 Gateway 做协议实验和定向调用。

### `debug_replication.py`

复制链路调试脚本，用于观察对象更新下发。

### `server_control_api.py`

本地或远端常驻 Agent，提供构建、起停服、验证、日志和任务查询 API。

### `server_registry.py`

多机场景下的中心注册服务，负责心跳和节点表。

### `server_manager_tui.py`

零依赖终端控制台，聚合本地和多机控制入口。

## 推荐使用顺序

1. 编译项目
2. 跑 `python3 Scripts/compile_assets.py --build`
3. 跑 `validate.py`
4. 需要长期观察服务时，再用 `servers.py start`
5. 协议变更时补跑 `verify_protocol.py`

## 相关文档

- [BuildAndRun.md](/root/Mession/Docs/BuildAndRun.md)
- [Validation.md](/root/Mession/Docs/Validation.md)
- [Tooling.md](/root/Mession/Docs/Tooling.md)
- [ServerControlApi.md](/root/Mession/Docs/ServerControlApi.md)
- [ServerRegistry.md](/root/Mession/Docs/ServerRegistry.md)
- [ServerManagerTui.md](/root/Mession/Docs/ServerManagerTui.md)
