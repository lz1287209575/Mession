# 验证策略

## 当前推荐验证层次

### 第 1 层

先过编译：

```bash
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
cmake --build Build -j4
```

### 第 2 层

再过最小链路：

```bash
python3 Scripts/validate.py --build-dir Build --no-build
```

### 第 3 层

需要手工观察某个服务时：

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

然后验证：

- `Client_Login`
- `Client_FindPlayer`
- `Client_SwitchScene`
- `Client_Logout`

## 为什么以这条链路为主

因为它同时穿过了当前架构里最关键的几个收敛点：

- Gateway 客户端入口
- 统一 `MT_FunctionCall`
- Login 会话签发
- World 玩家对象树
- Scene 切场调用
- Router 路由协作
- Mgo 持久化边界

如果这条链路稳定，说明当前主骨架是可工作的。

## 建议的改动后验证

### 改 `Protocol`

- 跑编译
- 跑 `Scripts/verify_protocol.py`
- 跑 `validate.py`

### 改 `Rpc`

- 跑编译
- 跑 `validate.py`
- 必要时用 `test_client.py` 做定向调用

### 改 `World` 对象状态

- 跑编译
- 跑 `validate.py`
- 跑 `debug_replication.py` 或观察 `WorldServer` 日志

### 改 `Persistence / Replication`

- 跑编译
- 跑 `validate.py`
- 重点检查对象 dirty 清理、版本号推进、对象快照是否合理

## 当前验证空白

目前仓库仍缺少更系统的自动化测试：

- 单元测试覆盖不足
- 并发运行时缺少专门测试
- 复制与持久化缺少更细粒度回归测试
- 多客户端、多玩家、多服故障场景还未建立固定回归集

这也是后续需要补齐的部分。
