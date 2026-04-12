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
2. 跑 `validate.py`
3. 需要长期观察服务时，再用 `servers.py start`
4. 协议变更时补跑 `verify_protocol.py`

## 相关文档

- [BuildAndRun.md](/root/Mession/Docs/BuildAndRun.md)
- [Validation.md](/root/Mession/Docs/Validation.md)
- [Tooling.md](/root/Mession/Docs/Tooling.md)
- [ServerControlApi.md](/root/Mession/Docs/ServerControlApi.md)
- [ServerRegistry.md](/root/Mession/Docs/ServerRegistry.md)
- [ServerManagerTui.md](/root/Mession/Docs/ServerManagerTui.md)
