# Scripts 目录说明

`Scripts/` 放的是开发和验证辅助脚本，不承载正式服务器逻辑。

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

适合本地调试和看日志。

### `validate.py`

最小完整链路验证：

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

- `Client_Login`
- `Client_FindPlayer`
- `Client_SwitchScene`
- `Client_Logout`

### `verify_protocol.py`

协议相关检查脚本，适合改消息结构或 RPC 元数据后运行。

### `test_client.py`

轻量测试客户端，用于手工试协议。

### `debug_replication.py`

复制链路调试脚本。

## 推荐使用顺序

1. 编译项目
2. 跑 `validate.py`
3. 若需长期观察服务，再用 `servers.py start`

## 相关文档

- [BuildAndRun.md](/root/Mession/Docs/BuildAndRun.md)
- [Validation.md](/root/Mession/Docs/Validation.md)
- [Tooling.md](/root/Mession/Docs/Tooling.md)
